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

#define HTTP_PORT 8000

static const char *outputDir = "results";
static const char *outputFile = "network.csv";
static TopoMap_Config config;

static void writeToCSVFile(NodeRoutingTable table);
static int killProcessOnPort(int port);
static void installDependencies();
static void createCSVFile();
static void generateGraph();
static void createHttpServer(int port);
static void *recvRoutingTable_func(void *args);
static void *sendRoutingTable_func(void *args);

static void writeToCSVFile(NodeRoutingTable table)
{
    FILE *file = fopen(outputFile, "a");
    if (file == NULL)
    {
        printf("## - Error opening csv file!\n");
        exit(EXIT_FAILURE);
    }
    if (config.loglevel >= DEBUG)
    {
        printf("# %s - Writing to CSV\n", timestamp());
        printf("# Timestamp, Source, Address, State, Role, RSSI, Parent, ParentRSSI\n");
    }
    for (int i = 0; i < table.numNodes; i++)
    {
        NodeInfo node = table.nodes[i];
        fprintf(file, "%d,%02d,%02d,%s,%s,%d,%02d,%d\n", node.lastSeen, table.src, node.addr, getNodeStateStr(node.state), getNodeRoleStr(node.role), node.RSSI, node.parent, node.parentRSSI);
        if (config.loglevel >= DEBUG)
        {
            printf("# %d, %02d, %02d, %s, %s, %d, %02d, %d\n", node.lastSeen, table.src, node.addr, getNodeStateStr(node.state), getNodeRoleStr(node.role), node.RSSI, node.parent, node.parentRSSI);
        }
    }
    fclose(file);
    fflush(stdout);
}

static void createCSVFile()
{
    char cmd[150];
    sprintf(cmd, "[ -d '%s' ] || mkdir -p '%s' && cp '../logs/index.html' '%s/index.html'", outputDir, outputDir, outputDir);
    if (system(cmd) != 0)
    {
        printf("## - Error creating results dir!\n");
        exit(EXIT_FAILURE);
    }
    char filePath[100];
    sprintf(filePath, "%s/%s", outputDir, outputFile);
    FILE *file = fopen(filePath, "w");
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
        printf("# %s - CSV file: %s created\n", timestamp(), outputFile);
    }
}

static void generateGraph()
{
    printf("%s - Generating graph. Open http://localhost:8000 \n", timestamp());
    char cmd[100];
    sprintf(cmd, "python ../../logs/script.py %d", ADDR_SINK);
    if (system(cmd) != 0)
    {
        printf("# Error generating graph...\n");
    }
}

static void installDependencies()
{

    if (config.loglevel >= DEBUG)
    {
        printf("# %s - Installing dependencies...\n", timestamp());
        fflush(stdout);
    }
    // chdir("/home/pi/sw_workspace/AlohaRoute/Debug");
    char *setenv_cmd = "pip install -r ../logs/requirements.txt > /dev/null &";
    if (config.loglevel == TRACE)
    {
        printf("## %s - Executing command : %s\n", timestamp(), setenv_cmd);
    }
    fflush(stdout);
    if (system(setenv_cmd) != 0)
    {
        printf("### Error installing dependencies. Exiting...\n");
        fflush(stdout);
        exit(EXIT_FAILURE);
    }
    if (config.loglevel >= DEBUG)
    {
        printf("%s - # Successfully installed dependencies...\n", timestamp());
    }
}

// Check if a process is running on the specified TCP port and kill it
int killProcessOnPort(int port)
{
    char cmd[100];
    sprintf(cmd, "fuser -k %d/tcp > /dev/null 2>&1", port);
    if (config.loglevel >= DEBUG)
    {
        printf("# %s - Port %d freed\n", timestamp(), port);
    }
}

static void createHttpServer(int port)
{

    chdir(outputDir);
    killProcessOnPort(port);
    char cmd[100];
    sprintf(cmd, "python3 -m http.server %d --bind 0.0.0.0 > httplogs.txt 2>&1 &", port);
    if (system(cmd) != 0)
    {
        printf("### Error starting HTTP server\n");
        fclose(stdout);
        fclose(stderr);
        exit(EXIT_FAILURE);
    }

    if (config.loglevel >= DEBUG)
    {
        printf("# %s - HTTP server started on port: %d\n", timestamp(), 8000);
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
                printf("# %s - STRP_sendRoutingTable : No active neighbours\n");
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
            printf("### Error: Failed to create sendRoutingTable thread");
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        installDependencies();
        createCSVFile();
        createHttpServer(HTTP_PORT);
        pthread_t recvRoutingTableT;
        if (pthread_create(&recvRoutingTableT, NULL, recvRoutingTable_func, NULL) != 0)
        {
            printf("### Error: Failed to create recvRoutingTable thread");
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

        int result = STRP_timedRecvRoutingTable(&header, &table, 1);
        if (result)
        {
            writeToCSVFile(table);
        }
        time_t current = time(NULL);
        if (current - start > config.graphUpdateIntervalS)
        {
            NodeRoutingTable t = getSinkRoutingTable();
            if (t.numNodes > 0)
            {
                writeToCSVFile(t);
                generateGraph();
                start = current;
            }
        }
        sleep(2);
    }
    return NULL;
}
