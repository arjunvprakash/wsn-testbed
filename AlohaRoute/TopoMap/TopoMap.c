#include "TopoMap.h"

#include <stdio.h>   // printf
#include <stdlib.h>  // rand, malloc, free, exit
#include <unistd.h>  // sleep, exec, chdir
#include <string.h>  // memcpy, strerror, strrok
#include <pthread.h> // pthread_create
#include <time.h>    // time

#include "../common.h"
#include "../util.h"
#include "../STRP/STRP.h"

static const char *outputCSV = "/home/pi/sw_workspace/AlohaRoute/Debug/results/network.csv";
static TopoMap_Config config;

static void writeToCSVFile(NodeRoutingTable table);
static int killProcessOnPort(int port);
static void installDependencies();
static void createCSVFile();
static void generateGraph();
static void createHttpServer();
static void *recvRoutingTable_func(void *args);
static void *sendRoutingTable_func(void *args);

static void writeToCSVFile(NodeRoutingTable table)
{
    FILE *file = fopen(outputCSV, "a");
    if (file == NULL)
    {
        printf("## - Error opening csv file!\n");
        exit(EXIT_FAILURE);
    }
    if (config.loglevel >= DEBUG)
    {
        printf("### Writing to CSV\n");
        printf("Timestamp, Source, Address, State, Role, RSSI, Parent, ParentRSSI\n");
    }
    for (int i = 0; i < table.numActive; i++)
    {
        NodeInfo node = table.nodes[i];
        fprintf(file, "%s,%02d,%02d,%s,%s,%d,%02d,%d\n", table.timestamp, table.src, node.addr, getNodeStateStr(node.state), getNodeRoleStr(node.role), node.RSSI, node.parent, node.parentRSSI);
        if (config.loglevel >= DEBUG)
        {
            printf("%s, %02d, %02d, %s, %s, %d, %02d, %d\n", table.timestamp, table.src, node.addr, getNodeStateStr(node.state), getNodeRoleStr(node.role), node.RSSI, node.parent, node.parentRSSI);
        }
    }
    fclose(file);
    fflush(stdout);
}

static void createCSVFile()
{
    const char *cmd = "[ -d '/home/pi/sw_workspace/AlohaRoute/Debug/results' ] || mkdir -p '/home/pi/sw_workspace/AlohaRoute/Debug/results' && cp '/home/pi/sw_workspace/AlohaRoute/logs/index.html' '/home/pi/sw_workspace/AlohaRoute/Debug/results/index.html'";
    if (system(cmd) != 0)
    {
        printf("## - Error creating results dir!\n");
        exit(EXIT_FAILURE);
    }
    FILE *file = fopen(outputCSV, "w");
    if (file == NULL)
    {
        printf("## - Error creating csv file!\n");
        exit(EXIT_FAILURE);
    }
    const char *header = "Timestamp,Source,Address,State,Role,RSSI,Parent,ParentRSSI\n";
    fprintf(file, "%s", header);
    fclose(file);
    if (config.loglevel >= DEBUG)
    {
        printf("### CSV file: %s created\n", outputCSV);
    }
}

static void generateGraph()
{
    printf("%s - Generating graph. Open http://localhost:8000 \n", timestamp());
    char cmd[100];
    sprintf(cmd, "python /home/pi/sw_workspace/AlohaRoute/logs/script.py %d", ADDR_SINK);
    if (system(cmd) != 0)
    {
        printf("### Error generating graph...\n");
    }
}

static void installDependencies()
{

    if (config.loglevel >= DEBUG)
    {
        printf("### Installing dependencies...\n");
        fflush(stdout);
    }
    chdir("/home/pi/sw_workspace/AlohaRoute/Debug");
    char *setenv_cmd = "pip install -r /home/pi/sw_workspace/AlohaRoute/logs/requirements.txt > /dev/null &";
    if (config.loglevel == TRACE)
    {
        printf("### Executing command : %s\n", setenv_cmd);
    }
    fflush(stdout);
    if (system(setenv_cmd) != 0)
    {
        printf("## Error installing dependencies. Exiting...\n");
        fflush(stdout);
        exit(EXIT_FAILURE);
    }
    if (config.loglevel >= DEBUG)
    {
        printf("### Successfully installed dependencies...\n");
    }
}

// Check if a process is running on the specified TCP port and kill it
int killProcessOnPortV1(int port)
{
    char cmd[100];
    sprintf(cmd, "fuser -k %d/tcp > /dev/null", port);
    FILE *fp = popen(cmd, "r");
    if (fp != NULL)
    {
        char buffer[256];
        if (fgets(buffer, sizeof(buffer), fp) != NULL)
        {
            char *process_id = strtok(buffer, " ");
            char kill_cmd[100];
            sprintf(kill_cmd, "kill -9 %s", process_id);
            system(kill_cmd);
            printf("Process %s killed.\n", process_id);
        }
        pclose(fp);
    }
}

int killProcessOnPort(int port)
{
    char cmd[100];
    sprintf(cmd, "fuser -k %d/tcp > /dev/null 2>&1", port);
    if (config.loglevel >= DEBUG)
    {
        printf("### Port %d freed\n", port);
    }
}

static void createHttpServer()
{

    chdir("/home/pi/sw_workspace/AlohaRoute/Debug/results");

    killProcessOnPort(8000);
    char *cmd = "python3 -m http.server 8000 --bind 0.0.0.0 > /dev/null 2>&1 &";
    if (system(cmd) != 0)
    {
        printf("## Error starting HTTP server\n");
        fclose(stdout);
        fclose(stderr);
        exit(EXIT_FAILURE);
    }

    if (config.loglevel >= DEBUG)
    {
        printf("### HTTP server started on port: %d\n", 8000);
    }
}

static void *sendRoutingTable_func(void *args)
{
    sleep(config.routingTableIntervalS);
    while (1)
    {

        if (STRP_sendRoutingTable() == 0)
        {
            if (config.loglevel >= DEBUG)
            {
                printf("%s - STRP_sendRoutingTable : No active neighbours\n");
            }
        }
        sleep(config.routingTableIntervalS);
    }
    return NULL;
}

void TopoMap_init(TopoMap_Config c)
{
    config = c;
    if (config.self != ADDR_SINK)
    {
        pthread_t sendRoutingTableT;
        if (pthread_create(&sendRoutingTableT, NULL, sendRoutingTable_func, NULL) != 0)
        {
            printf("## Error: Failed to create sendRoutingTable thread");
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        installDependencies();
        createCSVFile();
        createHttpServer();
        pthread_t recvRoutingTableT;
        if (pthread_create(&recvRoutingTableT, NULL, recvRoutingTable_func, NULL) != 0)
        {
            printf("## Error: Failed to create recvRoutingTable thread");
            exit(EXIT_FAILURE);
        }
    }
}

static void *recvRoutingTable_func(void *args)
{
    time_t start = time(NULL);
    while (1)
    {
        NodeRoutingTable table;
        STRP_Header header;

        int result = STRP_timedRecvRoutingTable(&header, &table, 2);
        if (result <= 0)
        {
            continue;
        }
        writeToCSVFile(table);
        time_t current = time(NULL);
        if (current - start > config.graphUpdateIntervalS)
        {
            generateGraph();
            start = current;
        }
        sleep(2);
    }
    return NULL;
}
