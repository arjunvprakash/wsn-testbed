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

#define HTTP_PORT 8000
#define MAX_PAYLOAD_SIZE 240

typedef enum
{
    CTRL_MSG = '\x71',
    CTRL_TAB = '\x72',
    CTRL_MAC = '\x73',
    CTRL_ROU = '\x78'
} CTRL;

#define ROUTING_OVERHEAD_SIZE (sizeof(uint8_t) + sizeof(uint8_t) + sizeof(time_t)) // ctrl, numHops, timestamp
#define MAC_OVERHEAD_SIZE (sizeof(time_t))                                         // timestamp

typedef struct MAC_Data
{
    uint16_t totalLatency;
    uint16_t totalRecv;
    uint8_t pdr;
    uint16_t totalSent;
    uint16_t collisions;
} MAC_Data;

typedef struct Routing_Data
{
    uint16_t numHops;
    uint16_t totalSent;
    uint16_t totalRecv;
    uint16_t totalLatency;
} Routing_Data;

typedef struct MACMetrics
{
    MAC_Data data[MAX_ACTIVE_NODES];
    uint8_t minAddr, maxAddr;
    time_t timestamp;
} MACMetrics;

typedef struct RoutingMetrics
{
    Routing_Data data[MAX_ACTIVE_NODES];
    uint8_t minAddr, maxAddr;
    time_t timestamp;
} RoutingMetrics;

static int (*Original_Routing_sendMsg)(uint8_t dest, uint8_t *data, unsigned int len) = NULL;
static int (*Original_Routing_recvMsg)(Routing_Header *h, uint8_t *data) = NULL;
static int (*Original_Routing_timedRecvMsg)(Routing_Header *h, uint8_t *data, unsigned int timeout) = NULL;

static int (*Original_MAC_sendMsg)(MAC *, unsigned char dest, unsigned char *data, unsigned int len) = NULL;
static int (*Original_MAC_recvMsg)(MAC *, unsigned char *data) = NULL;
static int (*Original_MAC_timedRecvMsg)(MAC *, unsigned char *data, unsigned int timeout) = NULL;

static const char *outputDir = "results";
static const char *networkCSV = "network.csv";
static const char *macCSV = "mac.csv";
static const char *routingCSV = "routing.csv";

static ProtoMon_Config config;
MACMetrics macMetrics = {0};
RoutingMetrics routingMetrics = {0};

static void writeToCSVFile(NodeRoutingTable table);
static int killProcessOnPort(int port);
static void installDependencies();
static void initResults();
static void generateGraph();
static void createHttpServer(int port);
static void *recvRoutingTable_func(void *args);
static void *sendRoutingTable_func(void *args);
static int writeBufferToFile(const uint8_t *fileName, uint8_t *temp);
static int sendMetricsToSink(uint8_t *buffer, unsigned int len, CTRL ctrl);
static void getMetricsBuffer(uint8_t **buffer, uint16_t bufferSize, CTRL ctrl);
static uint16_t getRoutingOverhead();
static uint16_t getMACOverhead();

static void writeToCSVFile(NodeRoutingTable table)
{
    FILE *file = fopen(networkCSV, "a");
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

static void initResults()
{
    // Create results dir
    char cmd[150];
    sprintf(cmd, "[ -d '%s' ] || mkdir -p '%s' && cp '../logs/index.html' '%s/index.html'", outputDir, outputDir, outputDir);
    if (system(cmd) != 0)
    {
        printf("## - Error creating results dir!\n");
        exit(EXIT_FAILURE);
    }

    // Create network.csv
    char filePath[100];
    sprintf(filePath, "%s/%s", outputDir, networkCSV);
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
        printf("# %s - CSV file: %s created\n", timestamp(), networkCSV);
    }

    // Create mac.csv
    if (config.monitoredLayers & PROTOMON_LAYER_MAC)
    {
        sprintf(filePath, "%s/%s", outputDir, macCSV);
        file = fopen(filePath, "w");
        if (file == NULL)
        {
            printf("## - Error creating mac.csv file!\n");
            exit(EXIT_FAILURE);
        }
        const char *header = "Timestamp,Source,Address,TotalSent,TotalRecv,AvgLatency\n";
        fprintf(file, "%s", header);
        fclose(file);
        if (config.loglevel >= DEBUG)
        {
            printf("# %s - CSV file: mac.csv created\n", timestamp());
        }
    }

    // Create routing.csv
    if (config.monitoredLayers & PROTOMON_LAYER_ROUTING)
    {
        sprintf(filePath, "%s/%s", outputDir, routingCSV);
        file = fopen(filePath, "w");
        if (file == NULL)
        {
            printf("## - Error creating routing.csv file!\n");
            exit(EXIT_FAILURE);
        }
        const char *header = "Timestamp,Source,Address,TotalSent,TotalRecv,NumHops,AvgLatency\n";
        fprintf(file, "%s", header);
        fclose(file);
        if (config.loglevel >= DEBUG)
        {
            printf("# %s - CSV file: routing.csv created\n", timestamp());
        }
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

static int sendMetricsToSink(uint8_t *buffer, unsigned int len, CTRL ctrl)
{
    const unsigned int extLen = len + sizeof(uint8_t);
    uint8_t extBuffer[extLen];

    // Set control flag: MAC
    uint8_t *temp = extBuffer;
    uint8_t ctrlFlag = ctrl;
    memcpy(temp, &ctrlFlag, sizeof(ctrlFlag));
    temp += sizeof(ctrlFlag);

    memcpy(temp, buffer, len);
    return Original_Routing_sendMsg(ADDR_SINK, extBuffer, extLen);
}

static void getMetricsBuffer(uint8_t **buffer, uint16_t bufferSize, CTRL ctrl)
{
    size_t usedSize = 0;
    // (*buffer)[0] = '\0'; // Initialize buffer with an empty string

    if (ctrl == CTRL_MAC)
    {
        MACMetrics metrics = macMetrics;

        // Reset metrics
        macMetrics = (MACMetrics){0};

        metrics.timestamp = time(NULL);
        for (uint8_t i = metrics.minAddr; i <= metrics.maxAddr; i++)
        {
            // Generate CSV row for each non zero node
            const MAC_Data data = metrics.data[i];
            if (data.totalSent > 0 || data.totalRecv > 0)
            {
                uint8_t row[150];
                int rowLen = sprintf(row, "%ld,%02d,%02d,%d,%d,%d\n", (long)metrics.timestamp, config.self, i, data.totalSent, data.totalRecv, data.totalRecv > 0 ? (data.totalLatency / data.totalRecv) : 0);

                // clearing the timestamp to save packet size
                metrics.timestamp = 0L;

                if (usedSize + rowLen < bufferSize)
                {
                    memcpy(*buffer + usedSize, row, rowLen);
                    usedSize += rowLen;
                    // size_t newBufferSize = bufferSize * 2;
                    // uint8_t *newBuffer = (uint8_t *)realloc(*buffer, newBufferSize);
                    // if (newBuffer == NULL)
                    // {
                    //     printf("## - Error reallocating memory for MAC metrics buffer!\n");
                    //     exit(EXIT_FAILURE);
                    // }
                    // *buffer = newBuffer;
                    // bufferSize = newBufferSize;
                }
                else
                {
                    break;
                }

                // add terminating null character
                // (*buffer)[usedSize] = '\0';
            }
        }
    }
    else if (ctrl == CTRL_ROU)
    {
        RoutingMetrics metrics = routingMetrics;

        // Reset metrics
        routingMetrics = (RoutingMetrics){0};

        metrics.timestamp = time(NULL);
        for (uint8_t i = metrics.minAddr; i <= metrics.maxAddr; i++)
        {
            const Routing_Data data = metrics.data[i];
            // Generate CSV row for each non zero node
            if (data.totalSent > 0 || data.totalRecv > 0)
            {
                uint8_t row[150];
                int rowLen = sprintf(row, "%ld,%02d,%02d,%d,%d,%d,%d\n", (long)metrics.timestamp, config.self, i, data.totalSent, data.totalRecv, data.numHops, data.totalRecv > 0 ? (data.totalLatency / data.totalRecv) : 0);

                // clearing the timestamp to save packet size
                metrics.timestamp = 0L;

                if (usedSize + rowLen < bufferSize)
                {
                    memcpy(*buffer + usedSize, row, rowLen);
                    usedSize += rowLen;
                    // size_t newBufferSize = bufferSize * 2;
                    // uint8_t *newBuffer = (uint8_t *)realloc(*buffer, newBufferSize);
                    // if (newBuffer == NULL)
                    // {
                    //     printf("## - Error reallocating memory for Routing metrics buffer!\n");
                    //     exit(EXIT_FAILURE);
                    // }
                    // *buffer = newBuffer;
                    // bufferSize = newBufferSize;
                }
                else
                {
                    break;
                }
                // add terminating null character
                // (*buffer)[usedSize] = '\0';
            }
        }
    }
    // add terminating null character
    (*buffer)[usedSize] = '\0';
    printf("### %s: %zu\n%s\n", ctrl == CTRL_MAC ? "MAC" : "Routing", usedSize, *buffer);
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
                printf("# %s - STRP_sendRoutingTable : No active neighbours\n", timestamp());
            }
        }
        // Send routing metrics to sink
        if (config.monitoredLayers & PROTOMON_LAYER_ROUTING)
        {
            uint16_t bufferSize = MAX_PAYLOAD_SIZE;
            uint8_t *buffer = (uint8_t *)malloc(bufferSize * sizeof(uint8_t));
            if (buffer == NULL)
            {
                printf("## - Error allocating memory for routing metrics buffer!\n");
                exit(EXIT_FAILURE);
            }
            getMetricsBuffer(&buffer, bufferSize, CTRL_ROU);
            if (buffer[0] != '\0')
            {
                if (!sendMetricsToSink(buffer, strlen(buffer), CTRL_ROU))
                {
                    printf("### Error: Failed to send Routing metrics to sink\n");
                }
                else
                {
                    // if (config.loglevel >= DEBUG)
                    {
                        printf("# %s - Sent Routing metrics to sink\n", timestamp());
                    }
                }
            }
            free(buffer);
        }

        // Send MAC metrics to sink
        if (config.monitoredLayers & PROTOMON_LAYER_MAC)
        {
            uint16_t bufferSize = MAX_PAYLOAD_SIZE;
            uint8_t *buffer = (uint8_t *)malloc(bufferSize * sizeof(uint8_t));
            if (buffer == NULL)
            {
                printf("## - Error allocating memory for routing metrics buffer!\n");
                exit(EXIT_FAILURE);
            }
            getMetricsBuffer(&buffer, bufferSize, CTRL_MAC);
            if (buffer[0] != '\0')
            {
                if (!sendMetricsToSink(buffer, strlen(buffer), CTRL_MAC))
                {
                    printf("### Error: Failed to send MAC metrics to sink\n");
                }
                else
                {
                    // if (config.loglevel >= DEBUG)
                    {
                        printf("# %s - Sent MAC metrics to sink\n", timestamp());
                    }
                }
            }
            free(buffer);
        }

        sleep(config.routingTableIntervalS);
    }
    return NULL;
}

// Move the sendrouting table logic from STRP.c to ProtoMon.c

void ProtoMon_init(ProtoMon_Config c)
{

    if (c.monitoredLayers & PROTOMON_LAYER_ROUTING)
    {
        if (!Original_Routing_sendMsg || !Original_Routing_recvMsg || !Original_Routing_timedRecvMsg || !Original_MAC_sendMsg || !Original_MAC_recvMsg || !Original_MAC_timedRecvMsg)
        {
            printf("### ProtoMon Error: Functions of routing & MAC layers must be registered.\n");
            exit(EXIT_FAILURE);
        }
        printf("%s - Monitoring enabled for Routing layer\n", timestamp());
        fflush(stdout);
        Routing_sendMsg = &ProtoMon_Routing_sendMsg;
        Routing_recvMsg = &ProtoMon_Routing_recvMsg;
        Routing_timedRecvMsg = &ProtoMon_Routing_timedRecvMsg;
        MAC_send = &ProtoMon_MAC_send;
        MAC_recv = &ProtoMon_MAC_recv;
        MAC_timedRecv = &ProtoMon_MAC_timedRecv;
    }

    if (c.monitoredLayers & PROTOMON_LAYER_MAC)
    {
        if (!Original_MAC_sendMsg || !Original_MAC_recvMsg || !Original_MAC_timedRecvMsg)
        {
            printf("### ProtoMon Error: Functions of MAC layer must be registered.\n");
            exit(EXIT_FAILURE);
        }
        printf("%s - Monitoring enabled for MAC layer\n", timestamp());
        fflush(stdout);
        MAC_send = &ProtoMon_MAC_send;
        MAC_recv = &ProtoMon_MAC_recv;
        MAC_timedRecv = &ProtoMon_MAC_timedRecv;
    }

    config = c;

    // Enable visualization only when routing is enabled
    // if (c.monitoredLayers & PROTOMON_LAYER_ROUTING)
    {
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
            initResults();
            createHttpServer(HTTP_PORT);
            pthread_t recvRoutingTableT;
            if (pthread_create(&recvRoutingTableT, NULL, recvRoutingTable_func, NULL) != 0)
            {
                printf("### Error: Failed to create recvRoutingTable thread");
                exit(EXIT_FAILURE);
            }
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
        }
        sleep(2);
    }
    return NULL;
}

static uint16_t getRoutingOverhead()
{
    return (config.monitoredLayers & PROTOMON_LAYER_ROUTING) ? ROUTING_OVERHEAD_SIZE : 0;
}

static uint16_t getMACOverhead()
{
    return (config.monitoredLayers & PROTOMON_LAYER_MAC) ? MAC_OVERHEAD_SIZE : 0;
}

int ProtoMon_Routing_sendMsg(uint8_t dest, uint8_t *data, unsigned int len)
{
    time_t start = time(NULL);
    uint16_t overhead = getRoutingOverhead();
    if (overhead == 0)
    {
        return Original_Routing_sendMsg(dest, data, len); // No monitoring needed
    }
    uint8_t extData[overhead + len];
    const uint8_t numHops = 0;
    const time_t ts = time(NULL);
    uint8_t *temp = extData;

    // Set control flag: MSG
    uint8_t ctrl = CTRL_MSG;
    memcpy(temp, &ctrl, sizeof(ctrl));
    temp += sizeof(ctrl);

    // Set hopcount 0
    memcpy(temp, &numHops, sizeof(numHops));
    temp += sizeof(numHops);

    // Set timestamp
    memcpy(temp, &ts, sizeof(ts));
    temp += sizeof(ts);

    memcpy(temp, data, len);
    fflush(stdout);

    int ret = Original_Routing_sendMsg(dest, extData, overhead + len);

    // Capture metrics
    if (dest > routingMetrics.maxAddr)
    {
        routingMetrics.maxAddr = dest;
    }
    if (dest < routingMetrics.minAddr)
    {
        routingMetrics.minAddr = dest;
    }
    routingMetrics.data[dest].totalSent++;

    // printf("## %s: %ds\n", __func__, time(NULL) - start);
    return ret;
}

int ProtoMon_Routing_recvMsg(Routing_Header *header, uint8_t *data)
{
    //     uint16_t overhead = getRoutingOverhead();
    //     if (overhead == 0)
    //     {
    //         return Original_Routing_recvMsg(header, data);
    //     }
    //     uint8_t extendedData[overhead + 1024];
    //     int len = Original_Routing_recvMsg(header, extendedData);
    //     if (len <= 0)
    //     {
    //         return len;
    //     }

    //     uint8_t *temp = extendedData;
    //     uint8_t ctrl = *temp;
    //     if (ctrl == CTRL_MSG)
    //     {
    //         uint8_t src = header->src;
    //         // Extract routing monitoring fields
    //         uint8_t numHops;
    //         time_t ts;
    //         temp += sizeof(ctrl);
    //         memcpy(&numHops, temp, sizeof(numHops));
    //         temp += sizeof(numHops);
    //         memcpy(&ts, temp, sizeof(ts));
    //         temp += sizeof(ts);
    //         memcpy(data, temp, len - overhead);

    //         uint16_t latency = (time(NULL) - ts);
    //         printf("ProtoMon : %s hops: %d delay: %d s\n", data, numHops, latency);

    //         // Capture metrics
    //         if (src > routingMetrics.maxAddr)
    //         {
    //             routingMetrics.maxAddr = src;
    //         }
    //         if (src < routingMetrics.minAddr)
    //         {
    //             routingMetrics.minAddr = src;
    //         }
    //         routingMetrics.data[src].totalRecv++;
    //         routingMetrics.data[src].totalLatency += latency;
    //         routingMetrics.data[src].numHops = numHops;

    //         return len - overhead;
    //     }
    //     else if (ctrl == CTRL_MAC || ctrl == CTRL_ROU)
    //     {
    //         const char *fileName = (ctrl == CTRL_MAC) ? macCSV : routingCSV;
    //         temp += sizeof(ctrl);
    //         int len = writeBufferToFile(fileName, temp);
    //         if (len <= 0)
    //         {
    //             printf("### - Error writing to %s file!\n", fileName);
    //             exit(EXIT_FAILURE);
    //         }
    //         else
    //         {
    //             printf("### buffer len: %d\n", len);
    //             printf("%s - %s metrics received from Node %02d\n", timestamp(), (ctrl == CTRL_MAC) ? "MAC" : "Routing", header->src);
    //         }

    //         uint16_t bufferSize = 1024;
    //         uint8_t *buffer = (uint8_t *)malloc(bufferSize * sizeof(uint8_t));
    //         if (buffer == NULL)
    //         {
    //             printf("## - Error allocating memory for %s metrics buffer!\n", ctrl == CTRL_MAC ? "MAC" : "routing");
    //             exit(EXIT_FAILURE);
    //         }
    //         getMetricsBuffer(&buffer, bufferSize, ctrl);
    //         if (buffer[0] != '\0')
    //         {
    //             if (writeBufferToFile(fileName, buffer) <= 0)
    //             {
    //                 printf("### - Error writing to %s file!\n", fileName);
    //                 exit(EXIT_FAILURE);
    //             }
    //         }
    //         free(buffer);
    //         return 0;
    //     }
    return 0;
}

int ProtoMon_Routing_timedRecvMsg(Routing_Header *header, uint8_t *data, unsigned int timeout)
{
    time_t start = time(NULL);
    uint16_t overhead = getRoutingOverhead();
    if (overhead == 0)
    {
        return Original_Routing_timedRecvMsg(header, data, timeout);
    }
    // uint8_t extendedData[overhead + MAX_PAYLOAD_SIZE];
    uint8_t *extendedData = (uint8_t *)malloc((overhead + MAX_PAYLOAD_SIZE) * sizeof(uint8_t));
    if (extendedData == NULL)
    {
        printf("## - Error allocating memory for extendedData buffer!\n");
        exit(EXIT_FAILURE);
    }
    int len = Original_Routing_timedRecvMsg(header, extendedData, timeout);
    if (len <= 0)
    {
        return len;
    }

    uint8_t *temp = extendedData;
    uint8_t ctrl = *temp;
    if (ctrl == CTRL_MSG)
    {
        uint8_t src = header->src;
        // Extract routing monitoring fields
        uint8_t numHops;
        time_t ts;
        temp += sizeof(ctrl);
        memcpy(&numHops, temp, sizeof(numHops));
        temp += sizeof(numHops);
        memcpy(&ts, temp, sizeof(ts));
        temp += sizeof(ts);
        memcpy(data, temp, len - overhead);

        uint16_t latency = (time(NULL) - ts);
        printf("ProtoMon : %s hops: %d delay: %d s\n", data, numHops, latency);

        // Capture metrics
        if (src > routingMetrics.maxAddr)
        {
            routingMetrics.maxAddr = src;
        }
        if (src < routingMetrics.minAddr)
        {
            routingMetrics.minAddr = src;
        }
        routingMetrics.data[src].totalRecv++;
        routingMetrics.data[src].totalLatency += latency;
        routingMetrics.data[src].numHops = numHops;

        return len - overhead;
    }
    else if (ctrl == CTRL_MAC || ctrl == CTRL_ROU)
    {
        const char *fileName = (ctrl == CTRL_MAC) ? macCSV : routingCSV;
        temp += sizeof(ctrl);
        int len = writeBufferToFile(fileName, temp);
        if (len <= 0)
        {
            printf("### - Error writing to %s file!\n", fileName);
            exit(EXIT_FAILURE);
        }
        else
        {
            printf("### buffer len: %d\n", len);
            printf("%s - %s metrics received from Node %02d\n", timestamp(), (ctrl == CTRL_MAC) ? "MAC" : "Routing", header->src);
        }

        uint16_t bufferSize = MAX_PAYLOAD_SIZE;
        uint8_t *buffer = (uint8_t *)malloc(bufferSize * sizeof(uint8_t));
        if (buffer == NULL)
        {
            printf("## - Error allocating memory for %s metrics buffer!\n", ctrl == CTRL_MAC ? "MAC" : "routing");
            exit(EXIT_FAILURE);
        }
        getMetricsBuffer(&buffer, bufferSize, ctrl);
        if (buffer[0] != '\0')
        {
            if (writeBufferToFile(fileName, buffer) <= 0)
            {
                printf("### - Error writing to %s file!\n", fileName);
                exit(EXIT_FAILURE);
            }
        }
        free(buffer);
        return 0;
    }
    free(extendedData);
    return 0;
}

int ProtoMon_MAC_send(MAC *h, unsigned char dest, unsigned char *data, unsigned int len)
{
    uint16_t overhead = getMACOverhead();
    if (overhead == 0)
    {
        return Original_MAC_sendMsg(h, dest, data, len);
    }

    uint8_t extData[overhead + len];
    uint8_t *temp = extData;
    if (dest != ADDR_BROADCAST) // exclude broadcast messages - beacons
    {
        // Check if msg packet
        uint8_t ctrl;
        bool isMsg;
        if (getRoutingOverhead())
        {
            ctrl = *(data + Routing_getHeaderSize());
            isMsg = (ctrl == CTRL_MSG);
        }
        else
        {
            ctrl = *data;
            isMsg = Routing_isDataPkt(ctrl);
        }
        if (isMsg) // Monitor only msg packets
        {
            // Add hop timestamp
            time_t ts = time(NULL);
            memcpy(temp, &ts, sizeof(ts));
            temp += sizeof(ts);

            // Capture metrics
            if (dest != ADDR_BROADCAST)
            {
                if (dest > macMetrics.maxAddr)
                {
                    macMetrics.maxAddr = dest;
                }
                if (dest < macMetrics.minAddr)
                {
                    macMetrics.minAddr = dest;
                }
                macMetrics.data[dest].totalSent++;
            }
        }
    }
    memcpy(temp, data, len);

    time_t start = time(NULL);
    int ret = Original_MAC_sendMsg(h, dest, extData, overhead + len);

    // printf("## %s: %ds\n", __func__, time(NULL) - start);
    return ret;
}

int ProtoMon_MAC_recv(MAC *h, unsigned char *data)
{
    time_t start = time(NULL);
    uint16_t overhead = getMACOverhead();
    uint8_t extendedData[overhead + MAX_PAYLOAD_SIZE];
    int len = Original_MAC_recvMsg(h, extendedData);
    if (len <= 0)
    {
        return len;
    }
    uint8_t *temp = extendedData;
    uint8_t dest = h->recvH.dst_addr;
    if (dest != ADDR_BROADCAST) // exclude broadcasts - beacons
    {
        // Check if msg packet
        uint8_t ctrl;
        bool isMsg;
        uint16_t routingOverhead = getRoutingOverhead();
        if (routingOverhead)
        {
            ctrl = *(temp + overhead + Routing_getHeaderSize());
            isMsg = (ctrl == CTRL_MSG);
        }
        else
        {
            ctrl = *(temp + overhead);
            isMsg = Routing_isDataPkt(ctrl);
        }
        if (overhead && isMsg)
        {
            uint8_t src = h->recvH.src_addr;
            // extract hop timestamp
            time_t mac_ts;
            memcpy(&mac_ts, temp, sizeof(mac_ts));
            temp += sizeof(mac_ts);
            unsigned int latency = (time(NULL) - mac_ts);
            printf("ProtoMon : hop src:%02d latency:%ds\n", src, latency);

            // Capture metrics
            if (src > macMetrics.maxAddr)
            {
                macMetrics.maxAddr = src;
            }
            if (src < macMetrics.minAddr)
            {
                macMetrics.minAddr = src;
            }
            macMetrics.data[src].totalRecv++;
            macMetrics.data[src].totalLatency += latency;
        }

        if (routingOverhead && isMsg) // Monitor only msg packets
        {

            uint8_t *p = temp;
            uint8_t numHops;

            // Increment hopCount
            p += Routing_getHeaderSize();
            p += sizeof(ctrl);
            memcpy(&numHops, p, sizeof(numHops));
            numHops++;
            memcpy(p, &numHops, sizeof(numHops));
            p += sizeof(numHops);
        }
    }

    memcpy(data, temp, len - overhead);
    // printf("## %s: %ds\n", __func__, time(NULL) - start);

    return len - overhead;
}

int ProtoMon_MAC_timedRecv(MAC *h, unsigned char *data, unsigned int timeout)
{
    time_t start = time(NULL);
    uint16_t overhead = getMACOverhead();
    uint8_t extendedData[overhead + MAX_PAYLOAD_SIZE];
    int len = Original_MAC_timedRecvMsg(h, extendedData, timeout);
    if (len <= 0)
    {
        return len;
    }
    uint8_t *temp = extendedData;
    uint8_t dest = h->recvH.dst_addr;
    if (dest != ADDR_BROADCAST) // exclude broadcasts - beacons
    {
        // Check if msg packet
        uint8_t ctrl;
        bool isMsg;
        uint16_t routingOverhead = getRoutingOverhead();
        if (routingOverhead)
        {
            ctrl = *(temp + overhead + Routing_getHeaderSize());
            isMsg = (ctrl == CTRL_MSG);
        }
        else
        {
            ctrl = *(temp + overhead);
            isMsg = Routing_isDataPkt(ctrl);
        }
        if (overhead && isMsg)
        {
            uint8_t src = h->recvH.src_addr;
            // extract hop timestamp
            time_t mac_ts;
            memcpy(&mac_ts, temp, sizeof(mac_ts));
            temp += sizeof(mac_ts);
            unsigned int latency = (time(NULL) - mac_ts);
            printf("ProtoMon : hop src:%02d latency:%ds\n", src, latency);

            // Capture metrics

            if (src > macMetrics.maxAddr)
            {
                macMetrics.maxAddr = src;
            }
            if (src < macMetrics.minAddr)
            {
                macMetrics.minAddr = src;
            }
            macMetrics.data[src].totalRecv++;
            macMetrics.data[src].totalLatency += latency;
        }

        if (routingOverhead && isMsg) // Monitor only msg packets
        {
            uint8_t *p = temp;
            uint8_t numHops;

            // Increment hopCount
            p += Routing_getHeaderSize();
            p += sizeof(ctrl);
            memcpy(&numHops, p, sizeof(numHops));
            numHops++;
            memcpy(p, &numHops, sizeof(numHops));
            p += sizeof(numHops);
        }
    }

    memcpy(data, temp, len - overhead);
    // printf("## %s: %ds\n", __func__, time(NULL) - start);

    return len - overhead;
}

static int writeBufferToFile(const uint8_t *fileName, uint8_t *temp)
{
    printf("### %s: %ld\n%s\n", fileName, strlen(temp), temp);
    FILE *file = fopen(fileName, "a");
    if (file == NULL)
    {
        printf("## - Error opening %s file!\n", fileName);
        exit(EXIT_FAILURE);
    }
    int len = fprintf(file, "%s", temp);
    fclose(file);
    return len;
}

void ProtoMon_setOrigRSend(int (*func)(uint8_t, uint8_t *, unsigned int))
{
    if (Original_Routing_sendMsg == NULL)
    {
        Original_Routing_sendMsg = func;
    }
}

void ProtoMon_setOrigRRecv(int (*func)(Routing_Header *, uint8_t *))
{
    if (Original_Routing_recvMsg == NULL)
    {
        Original_Routing_recvMsg = func;
    }
}

void ProtoMon_setOrigRTimedRecv(int (*func)(Routing_Header *, uint8_t *, unsigned int))
{
    if (Original_Routing_timedRecvMsg == NULL)
    {
        Original_Routing_timedRecvMsg = func;
    }
}

void ProtoMon_setOrigMACSend(int (*func)(MAC *, unsigned char, unsigned char *, unsigned int))
{
    if (Original_MAC_sendMsg == NULL)
    {
        Original_MAC_sendMsg = func;
    }
}

void ProtoMon_setOrigMACRecv(int (*func)(MAC *, unsigned char *))
{
    if (Original_MAC_recvMsg == NULL)
    {
        Original_MAC_recvMsg = func;
    }
}

void ProtoMon_setOrigMACTimedRecv(int (*func)(MAC *, unsigned char *, unsigned int))
{
    if (Original_MAC_timedRecvMsg == NULL)
    {
        Original_MAC_timedRecvMsg = func;
    }
}
