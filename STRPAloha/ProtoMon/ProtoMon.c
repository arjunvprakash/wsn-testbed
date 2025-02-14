#include "ProtoMon.h"

#include <stdio.h>   // printf
#include <stdlib.h>  // rand, malloc, free, exit
#include <unistd.h>  // sleep, exec, chdir
#include <string.h>  // memcpy, strerror, strrok
#include <pthread.h> // pthread_create
#include <time.h>    // time
#include <errno.h>   // errno

#include "../common.h"
#include "../util.h"
// #include "../STRP/STRP.h"

#define HTTP_PORT 8000

#define MV_TAB '\xF0'
#define MV_PKT '\xF1'
#define HEADER_SIZE 10 // 2(numHops) + 8(timestamp)

static int (*Original_Routing_sendMsg)(uint8_t dest, uint8_t *data, unsigned int len) = NULL;
static int (*Original_Routing_recvMsg)(Routing_Header *h, uint8_t *data) = NULL;
static int (*Original_Routing_timedRecvMsg)(Routing_Header *h, uint8_t *data, unsigned int timeout) = NULL;

static int (*Original_MAC_sendMsg)(MAC *, unsigned char dest, unsigned char *data, unsigned int len) = NULL;
static int (*Original_MAC_recvMsg)(MAC *, unsigned char *data) = NULL;
static int (*Original_MAC_timedRecvMsg)(MAC *, unsigned char *data, unsigned int timeout) = NULL;

static const char *outputDir = "results";
static const char *outputFile = "network.csv";
static ProtoMon_Config config;

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
    // exit code check fails if no process was running
    int v = system(cmd);
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

void ProtoMon_init(ProtoMon_Config c)
{
    // ###
    if (!Original_Routing_sendMsg || !Original_Routing_recvMsg || !Original_Routing_timedRecvMsg)
    {
        printf("Error: Original send, recv & timedRecv functions must be set before init.\n");
        return;
    }
    Routing_sendMsg = &ProtoMon_Routing_sendMsg;
    Routing_recvMsg = &ProtoMon_Routing_recvMsg;
    Routing_timedRecvMsg = &ProtoMon_Routing_timedRecvMsg;
    MAC_send = &ProtoMon_MAC_send;
    MAC_recv = &ProtoMon_MAC_recv;
    MAC_timedRecv = &ProtoMon_MAC_timedRecv;

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
        Routing_Header header;

        int len = STRP_timedRecvRoutingTable(&header, &table, 1);
        if (len)
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

int ProtoMon_Routing_sendMsg(uint8_t dest, uint8_t *data, unsigned int len)
{
    uint16_t numHops = 0;
    time_t ts = time(NULL);
    uint8_t *extData = (uint8_t *)malloc(HEADER_SIZE + len);
    if (!extData)
    {
        printf("### %s - ProtoMon_Routing_sendMsg malloc failed\n", timestamp());
        return -1;
    }
    uint8_t *temp = extData;
    memcpy(temp, &numHops, sizeof(numHops));
    temp += sizeof(numHops);
    memcpy(temp, &ts, sizeof(ts));
    temp += sizeof(ts);
    memcpy(temp, data, len);
    Original_Routing_sendMsg(dest, extData, HEADER_SIZE + len);
    return 1;
}

int ProtoMon_Routing_recvMsg(Routing_Header *header, uint8_t *data)
{
    uint8_t *extendedData = (uint8_t *)malloc(HEADER_SIZE + 240);
    if (!extendedData)
    {
        printf("### %s - ProtoMon_Routing_recvMsg malloc failed\n", timestamp());
        return -1;
    }
    int len = Original_Routing_recvMsg(header, extendedData);
    if (len <= 0)
    {
        return len;
    }
    uint16_t numHops;
    time_t ts;
    memcpy(&numHops, extendedData, sizeof(numHops));
    extendedData += sizeof(numHops);
    memcpy(&ts, extendedData, sizeof(ts));
    extendedData += sizeof(ts);
    memcpy(data, extendedData, len - HEADER_SIZE);
    return len - HEADER_SIZE;
}

int ProtoMon_Routing_timedRecvMsg(Routing_Header *header, uint8_t *data, unsigned int timeout)
{
    uint8_t *extendedData = (uint8_t *)malloc(HEADER_SIZE + 240);
    if (!extendedData)
    {
        printf("### %s - ProtoMon_Routing_timedRecvMsg malloc failed\n", timestamp());
        return -1;
    }
    int len = Original_Routing_timedRecvMsg(header, extendedData, timeout);
    if (len <= 0)
    {
        free(extendedData);
        return len;
    }
    uint8_t *temp = extendedData;
    uint16_t numHops;
    time_t ts;
    memcpy(&numHops, temp, sizeof(numHops));
    temp += sizeof(numHops);
    memcpy(&ts, temp, sizeof(ts));
    temp += sizeof(ts);
    memcpy(data, temp, len - HEADER_SIZE);
    return len - HEADER_SIZE;
}

void ProtoMon_setOrigRSendMsg(int (*func)(uint8_t, uint8_t *, unsigned int))
{
    if (Original_Routing_sendMsg == NULL)
    {
        Original_Routing_sendMsg = func;
    }
}

void ProtoMon_setOrigRRecvMsg(int (*func)(Routing_Header *, uint8_t *))
{
    if (Original_Routing_recvMsg == NULL)
    {
        Original_Routing_recvMsg = func;
    }
}

void ProtoMon_setOrigRTimedRecvMsg(int (*func)(Routing_Header *, uint8_t *, unsigned int))
{
    if (Original_Routing_timedRecvMsg == NULL)
    {
        Original_Routing_timedRecvMsg = func;
    }
}

int ProtoMon_MAC_send(MAC *h, unsigned char dest, unsigned char *data, unsigned int len)
{
    return Original_MAC_sendMsg(h, dest, data, len);
}

int ProtoMon_MAC_recv(MAC *h, unsigned char *data)
{
    return Original_MAC_recvMsg(h, data);
}

int ProtoMon_MAC_timedRecv(MAC *h, unsigned char *data, unsigned int timeout)
{
    return Original_MAC_timedRecvMsg(h, data, timeout);
}

void ProtoMon_setOrigMACSendMsg(int (*func)(MAC *, unsigned char, unsigned char *, unsigned int))
{
    if (Original_MAC_sendMsg == NULL)
    {
        Original_MAC_sendMsg = func;
    }
}

void ProtoMon_setOrigMACRecvMsg(int (*func)(MAC *, unsigned char *))
{
    if (Original_MAC_recvMsg == NULL)
    {
        Original_MAC_recvMsg = func;
    }
}

void ProtoMon_setOrigMACTimedRecvMsg(int (*func)(MAC *, unsigned char *, unsigned int))
{
    if (Original_MAC_timedRecvMsg == NULL)
    {
        Original_MAC_timedRecvMsg = func;
    }
}

uint8_t ProtoMon_getHeaderSize()
{
    return HEADER_SIZE;
}
