#include "ProtoMon.h"

#include <stdio.h>   // printf
#include <stdlib.h>  // rand, malloc, free, exit
#include <unistd.h>  // sleep, exec, chdir
#include <string.h>  // memcpy, strerror, strrok
#include <pthread.h> // pthread_create
#include <time.h>    // time
#include <errno.h>   // errno
#include <signal.h>  // signal

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
    uint16_t latency;
    uint16_t recv;
    uint16_t sent;
    uint16_t broadcast;
} MAC_Data;

typedef struct Routing_Data
{
    uint16_t numHops;
    uint16_t sent;
    uint16_t recv;
    uint16_t totalLatency;
} Routing_Data;

typedef struct MACMetrics
{
    MAC_Data data[MAX_ACTIVE_NODES];
    uint8_t minAddr, maxAddr;
    sem_t mutex;
} MACMetrics;

typedef struct RoutingMetrics
{
    Routing_Data data[MAX_ACTIVE_NODES];
    uint8_t minAddr, maxAddr;
    sem_t mutex;
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
static MACMetrics macMetrics;
static RoutingMetrics routingMetrics;
static time_t startTime, lastVizTime, lastMacWrite, lastNeighborWrite, lastRoutingWrite;

static int ProtoMon_Routing_sendMsg(uint8_t dest, uint8_t *data, unsigned int len);
static int ProtoMon_Routing_recvMsg(Routing_Header *h, uint8_t *data);
static int ProtoMon_Routing_timedRecvMsg(Routing_Header *header, uint8_t *data, unsigned int timeout);

static int ProtoMon_MAC_send(MAC *h, unsigned char dest, unsigned char *data, unsigned int len);
static int ProtoMon_MAC_recv(MAC *h, unsigned char *data);
static int ProtoMon_MAC_timedRecv(MAC *h, unsigned char *data, unsigned int timeout);

static int killProcessOnPort(int port);
static void installDependencies();
static void initOutputFiles();
static void generateGraph();
static void createHttpServer(int port);
static void *recvRoutingTable_func(void *args);
static void *sendMetrics_func(void *args);
static int writeBufferToFile(const uint8_t *fileName, uint8_t *temp);
static int sendMetricsToSink(uint8_t *buffer, unsigned int len, CTRL ctrl);
static uint16_t getMetricsBuffer(uint8_t *buffer, uint16_t bufferSize, CTRL ctrl);
static uint16_t getRoutingOverhead();
static uint16_t getMACOverhead();
static void initMetrics();
static void resetMacMetrics();
static void resetRoutingMetrics();
static void signalHandler(int signum);

static void initOutputFiles()
{
    // Create results dir
    char cmd[150];
    sprintf(cmd, "[ -d '%s' ] || mkdir -p '%s' && cp '../logs/index.html' '%s/index.html'", outputDir, outputDir, outputDir);
    if (system(cmd) != 0)
    {
        printf("## - Error creating results dir!\n");
        exit(EXIT_FAILURE);
    }

    // Create mac.csv
    if (config.monitoredLayers & PROTOMON_LAYER_MAC)
    {
        char filePath[100];
        sprintf(filePath, "%s/%s", outputDir, macCSV);
        FILE *file = fopen(filePath, "w");
        if (file == NULL)
        {
            printf("## - Error creating mac.csv file!\n");
            exit(EXIT_FAILURE);
        }
        const char *header = "Timestamp,Source,Address,TotalSent,TotalRecv,AvgLatency";
        fprintf(file, "%s", header);
        fprintf(file, ",%s", MAC_getMetricsHeader());
        fprintf(file, "\n");
        fclose(file);
        if (config.loglevel >= DEBUG)
        {
            printf("# %s - CSV file: mac.csv created\n", timestamp());
        }
    }

    if (config.monitoredLayers & PROTOMON_LAYER_ROUTING)
    {
        // Create network.csv
        char filePath[100];
        sprintf(filePath, "%s/%s", outputDir, networkCSV);
        FILE *file = fopen(filePath, "w");
        if (file == NULL)
        {
            printf("## - Error creating csv file!\n");
            exit(EXIT_FAILURE);
        }
        const char *nwheader = "Timestamp,Source,Address,State,Role,RSSI,Parent,ParentRSSI\n";
        fprintf(file, "%s", nwheader);
        fclose(file);
        if (config.loglevel >= DEBUG)
        {
            printf("# %s - CSV file: %s created\n", timestamp(), networkCSV);
        }

        // Create routing.csv
        sprintf(filePath, "%s/%s", outputDir, routingCSV);
        file = fopen(filePath, "w");
        if (file == NULL)
        {
            printf("## - Error creating routing.csv file!\n");
            exit(EXIT_FAILURE);
        }
        const char *header = "Timestamp,Source,Address,TotalSent,TotalRecv,NumHops,AvgLatency";
        fprintf(file, "%s", header);
        fprintf(file, ",%s", Routing_getMetricsHeader());
        fprintf(file, "\n");
        fclose(file);
        if (config.loglevel >= DEBUG)
        {
            printf("# %s - CSV file: routing.csv created\n", timestamp());
        }
    }
}

static void generateGraph()
{
    char cmd[100];
    sprintf(cmd, "python ../../logs/script.py %d", ADDR_SINK);
    if (system(cmd) != 0)
    {
        printf("# Error generating graph...\n");
    }
    else
    {
        printf("%s - Generated visualisation. Go to http://localhost:8000 \n", timestamp());
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

    // if (config.loglevel >= DEBUG)
    {
        printf("%s - HTTP server started on port: %d\n", timestamp(), 8000);
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

static uint16_t getMetricsBuffer(uint8_t *buffer, uint16_t bufferSize, CTRL ctrl)
{
    uint16_t usedSize = 0;
    time_t timestamp = time(NULL);

    if (ctrl == CTRL_MAC)
    {
        MACMetrics metrics = macMetrics;

        // Reset metrics
        resetMacMetrics();

        for (uint8_t i = metrics.minAddr; i <= metrics.maxAddr; i++)
        {
            // Generate CSV row for each non zero node
            const MAC_Data data = metrics.data[i];
            if (data.sent > 0 || data.recv > 0)
            {
                uint8_t row[150];
                uint8_t extra[50];
                int extraLen = MAC_getMetricsData(extra, i);
                int rowLen = sprintf(row, "%ld,%d,%d,%d,%d,%d,%s\n", (long)timestamp, config.self, i, (data.sent + metrics.data[0].broadcast), data.recv, data.recv > 0 ? (data.latency / data.recv) : 0, extra);

                // clearing the timestamp to save packet size
                timestamp = 0L;

                if (usedSize + rowLen < bufferSize)
                {
                    memcpy(buffer + usedSize, row, rowLen);
                    usedSize += rowLen;
                }
                else
                {
                    if (config.loglevel >= DEBUG)
                    {
                        printf("# MAC metrics buffer overflow\n");
                    }
                    break;
                }
            }
        }
    }
    else if (ctrl == CTRL_ROU)
    {
        RoutingMetrics metrics = routingMetrics;

        // Reset metrics
        resetRoutingMetrics();

        for (uint8_t i = metrics.minAddr; i <= metrics.maxAddr; i++)
        {
            const Routing_Data data = metrics.data[i];
            // Generate CSV row for each non zero node
            if (data.sent > 0 || data.recv > 0)
            {
                uint8_t row[150];
                uint8_t extra[50];
                int extraLen = Routing_getMetricsData(extra, i);
                int rowLen = sprintf(row, "%ld,%0d,%d,%d,%d,%d,%d,%s\n", (long)timestamp, config.self, i, data.sent, data.recv, data.numHops, data.recv > 0 ? (data.totalLatency / data.recv) : 0, extra);

                // clearing the timestamp to save packet size
                timestamp = 0L;

                if (usedSize + rowLen < bufferSize)
                {
                    memcpy(buffer + usedSize, row, rowLen);
                    usedSize += rowLen;
                }
                else
                {
                    if (config.loglevel >= DEBUG)
                    {
                        printf("# Routing metrics buffer overflow\n");
                    }
                    break;
                }
            }
        }
    }
    else if (ctrl == CTRL_TAB)
    {
        usedSize += Routing_getNeighbourData(buffer, bufferSize);
    }
    // add terminating null character
    buffer[usedSize] = '\0';
    usedSize++;

    if (config.loglevel > DEBUG && usedSize > 1)
    {
        printf("# %s: %d\n%s\n", ctrl == CTRL_MAC ? "MAC" : (ctrl == CTRL_ROU ? "Routing" : "Table"), usedSize, buffer);
    }
    return usedSize > 1 ? usedSize : 0;
}

static void *sendMetrics_func(void *args)
{
    sleep(config.initialSendWaitS);
    uint16_t bufferSize = MAX_PAYLOAD_SIZE - (Routing_getHeaderSize() + MAC_getHeaderSize() + getMACOverhead());
    while (1)
    {
        // Send routing metrics to sink
        if (config.monitoredLayers & PROTOMON_LAYER_ROUTING)
        {
            uint8_t *buffer = (uint8_t *)malloc(bufferSize);
            uint16_t bufLen = getMetricsBuffer(buffer, bufferSize, CTRL_ROU);
            if (bufLen)
            {
                if (!sendMetricsToSink(buffer, bufLen, CTRL_ROU))
                {
                    printf("### Error: Failed to send Routing metrics to sink\n");
                }
                else
                {
                    // if (config.loglevel >= DEBUG)
                    {
                        printf("%s - Sent Routing metrics to sink\n", timestamp());
                    }
                }
            }
            free(buffer);

            buffer = (uint8_t *)malloc(bufferSize);
            if (buffer == NULL)
            {
                printf("## - Error allocating memory for neighbor info buffer!\n");
                exit(EXIT_FAILURE);
            }
            bufLen = getMetricsBuffer(buffer, bufferSize, CTRL_TAB);
            if (bufLen)
            {
                if (!sendMetricsToSink(buffer, bufLen, CTRL_TAB))
                {
                    printf("### Error: Failed to send neighbour metrics to sink\n");
                }
                else
                {
                    // if (config.loglevel >= DEBUG)
                    {
                        printf("%s - Sent neighbour data to sink\n", timestamp());
                    }
                }
            }
            free(buffer);
        }

        // Send MAC metrics to sink
        if (config.monitoredLayers & PROTOMON_LAYER_MAC)
        {
            uint8_t buffer[bufferSize];
            if (buffer == NULL)
            {
                printf("## - Error allocating memory for routing metrics buffer!\n");
                exit(EXIT_FAILURE);
            }
            uint16_t bufLen = getMetricsBuffer(buffer, bufferSize, CTRL_MAC);
            if (bufLen)
            {
                if (!sendMetricsToSink(buffer, bufLen, CTRL_MAC))
                {
                    printf("### Error: Failed to send MAC metrics to sink\n");
                }
                else
                {
                    // if (config.loglevel >= DEBUG)
                    {
                        printf("%s - Sent MAC metrics to sink\n", timestamp());
                    }
                }
            }
        }

        sleep(config.sendIntervalS);
    }
    return NULL;
}

static void signalHandler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM || signum == SIGABRT || signum == SIGSEGV || signum == SIGILL || signum == SIGFPE)
    {
        // if (config.monitoredLayers & PROTOMON_LAYER_ROUTING)
        {
            killProcessOnPort(HTTP_PORT);
            // sleep(2);
            // if (config.loglevel >= DEBUG)
            {
                printf("%s - Stopped HTTP server on port %d\n", timestamp(), HTTP_PORT);
            }
        }
        exit(EXIT_SUCCESS);
    }
}

void setConfigDefaults(ProtoMon_Config *c)
{
    if (c->loglevel == 0)
    {
        c->loglevel = INFO;
    }
    if (c->sendIntervalS == 0)
    {
        c->sendIntervalS = 30;
    }
    if (c->vizIntervalS == 0)
    {
        c->vizIntervalS = 60;
    }
    if (c->initialSendWaitS == 0)
    {
        c->initialSendWaitS = 30;
    }
}

/*

Initializes ProtoMon with the given configuration.
This function must be called BEFORE initializing the routing and MAC layers.

*/
void ProtoMon_init(ProtoMon_Config c)
{   
    // Make init idempotent
    if (config.self != 0)
    {
        return;
    } 

    if (c.monitoredLayers != PROTOMON_LAYER_NONE)
    {
        // Implicit registration of original functions
        Original_Routing_sendMsg = Routing_sendMsg;
        Original_Routing_recvMsg = Routing_recvMsg;
        Original_Routing_timedRecvMsg = Routing_timedRecvMsg;

        Original_MAC_recvMsg = MAC_recv;
        Original_MAC_timedRecvMsg = MAC_timedRecv;
        Original_MAC_sendMsg = MAC_send;

        // Must always override Routing layer functions to capture monitoring data
        Routing_sendMsg = &ProtoMon_Routing_sendMsg;
        Routing_recvMsg = &ProtoMon_Routing_recvMsg;
        Routing_timedRecvMsg = &ProtoMon_Routing_timedRecvMsg;

        // Must always override MAC functions to increment numHops
        MAC_send = &ProtoMon_MAC_send;
        MAC_recv = &ProtoMon_MAC_recv;
        MAC_timedRecv = &ProtoMon_MAC_timedRecv;
    }
    if (c.monitoredLayers & PROTOMON_LAYER_ROUTING)
    {
        if (!Original_Routing_sendMsg || !Original_Routing_recvMsg || !Original_Routing_timedRecvMsg || !Original_MAC_sendMsg || !Original_MAC_recvMsg || !Original_MAC_timedRecvMsg)
        {
            printf("### ProtoMon Error: Functions of routing & MAC layers must be registered.\n");
            exit(EXIT_FAILURE);
        }
        printf("%s - Monitoring enabled for Routing layer\n", timestamp());
        fflush(stdout);
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
    }

    // Set default values for config
    setConfigDefaults(&c);
    config = c;
    startTime = lastVizTime = time(NULL);
    initMetrics();

    // Enable visualization only when monitoring is enabled
    if (c.monitoredLayers != PROTOMON_LAYER_NONE)
    {
        if (config.self != ADDR_SINK)
        {
            pthread_t sendRoutingTableT;
            if (pthread_create(&sendRoutingTableT, NULL, sendMetrics_func, NULL) != 0)
            {
                printf("### Error: Failed to create sendRoutingTable thread");
                exit(EXIT_FAILURE);
            }
        }
        else
        {
            installDependencies();
            initOutputFiles();
            createHttpServer(HTTP_PORT);

            // Register signal handler to stop the HTTP server on exit
            signal(SIGINT, signalHandler);
            signal(SIGTERM, signalHandler);
            signal(SIGILL, signalHandler);
            signal(SIGABRT, signalHandler);
            signal(SIGSEGV, signalHandler);
            signal(SIGFPE, signalHandler);
        }
    }
}

static uint16_t getRoutingOverhead()
{
    return (config.monitoredLayers & PROTOMON_LAYER_ROUTING) ? ROUTING_OVERHEAD_SIZE : 0;
}

static uint16_t getMACOverhead()
{
    return (config.monitoredLayers & PROTOMON_LAYER_MAC) ? MAC_OVERHEAD_SIZE : 0;
}

static void resetMacMetrics()
{
    sem_wait(&macMetrics.mutex);
    macMetrics.minAddr = MAX_ACTIVE_NODES - 1;
    macMetrics.maxAddr = 0;
    memset(macMetrics.data, 0, sizeof(macMetrics.data));
    sem_post(&macMetrics.mutex);
}

static void resetRoutingMetrics()
{
    sem_wait(&routingMetrics.mutex);
    routingMetrics.minAddr = MAX_ACTIVE_NODES - 1;
    routingMetrics.maxAddr = 0;
    memset(routingMetrics.data, 0, sizeof(routingMetrics.data));
    sem_post(&routingMetrics.mutex);
}

static void initMetrics()
{
    sem_init(&macMetrics.mutex, 0, 1);
    resetMacMetrics();

    sem_init(&routingMetrics.mutex, 0, 1);
    resetRoutingMetrics();
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
    uint8_t ctrl = (uint8_t)CTRL_MSG;
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
    sem_wait(&routingMetrics.mutex);
    if (dest > routingMetrics.maxAddr)
    {
        routingMetrics.maxAddr = dest;
    }
    if (dest < routingMetrics.minAddr)
    {
        routingMetrics.minAddr = dest;
    }
    routingMetrics.data[dest].sent++;
    sem_post(&routingMetrics.mutex);

    return ret;
}

int ProtoMon_Routing_recvMsg(Routing_Header *header, uint8_t *data)
{
    return 0;
}

int ProtoMon_Routing_timedRecvMsg(Routing_Header *header, uint8_t *data, unsigned int timeout)
{
    time_t start = time(NULL);
    uint16_t overhead = getRoutingOverhead();
    uint8_t extendedData[MAX_PAYLOAD_SIZE];
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
        if (config.loglevel >= DEBUG)
        {
            printf("ProtoMon : %s hops: %d delay: %d s\n", data, numHops, latency);
        }

        // Capture metrics
        sem_wait(&routingMetrics.mutex);
        if (src > routingMetrics.maxAddr)
        {
            routingMetrics.maxAddr = src;
        }
        if (src < routingMetrics.minAddr)
        {
            routingMetrics.minAddr = src;
        }
        routingMetrics.data[src].recv++;
        routingMetrics.data[src].totalLatency += latency;
        routingMetrics.data[src].numHops = numHops;
        sem_post(&routingMetrics.mutex);

        return len - overhead;
    }
    else if (ctrl == CTRL_MAC || ctrl == CTRL_ROU || ctrl == CTRL_TAB)
    {
        if (config.self == ADDR_SINK)
        {
            const char *fileName = (ctrl == CTRL_MAC) ? macCSV : (ctrl == CTRL_TAB ? networkCSV : routingCSV);
            temp += sizeof(ctrl);
            int writeLen = writeBufferToFile(fileName, temp);
            if (writeLen <= 0)
            {
                printf("### - Error writing to %s file!\n", fileName);
                exit(EXIT_FAILURE);
            }
            else
            {
                printf("%s - %s data of Node %02d\n", timestamp(), (ctrl == CTRL_MAC) ? "MAC" : (ctrl == CTRL_TAB ? "neigbour" : "routing"), header->src);
            }

            // Write corresponding sink metrics to file
            time_t *lastWrite = (ctrl == CTRL_MAC) ? &lastMacWrite : (ctrl == CTRL_TAB ? &lastNeighborWrite : &lastRoutingWrite);
            if (time(NULL) - *lastWrite > config.sendIntervalS)
            {
                uint16_t bufferSize = MAX_PAYLOAD_SIZE;
                uint8_t buffer[bufferSize];
                if (buffer == NULL)
                {
                    printf("## - Error allocating memory for %s data buffer!\n", ctrl == CTRL_MAC ? "MAC" : (ctrl == CTRL_TAB ? "neigbour" : "routing"));
                    exit(EXIT_FAILURE);
                }
                uint16_t bufLen = getMetricsBuffer(buffer, bufferSize, ctrl);
                if (bufLen)
                {
                    if (writeBufferToFile(fileName, buffer) <= 0)
                    {
                        printf("### - Error writing to %s file!\n", fileName);
                        exit(EXIT_FAILURE);
                    }
                    *lastWrite = time(NULL);
                }
            }
        }
    }
    if (config.self == ADDR_SINK && time(NULL) - lastVizTime > config.vizIntervalS)
    {
        generateGraph();
        lastVizTime = time(NULL);
    }

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
    // if (dest != ADDR_BROADCAST) // exclude broadcast messages - beacons
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
        // if (isMsg) // Monitor only msg packets
        {
            // Add hop timestamp
            time_t ts = time(NULL);
            memcpy(temp, &ts, sizeof(ts));
            temp += sizeof(ts);

            // Capture metrics
            sem_wait(&macMetrics.mutex);
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
                macMetrics.data[dest].sent++;
            }
            else
            {
                macMetrics.data[0].broadcast++;
            }
            sem_post(&macMetrics.mutex);
        }
    }
    memcpy(temp, data, len);

    time_t start = time(NULL);
    int ret = Original_MAC_sendMsg(h, dest, extData, overhead + len);

    return ret;
}

int ProtoMon_MAC_recv(MAC *h, unsigned char *data)
{
    time_t start = time(NULL);
    uint16_t overhead = getMACOverhead();
    uint8_t extendedData[MAX_PAYLOAD_SIZE];
    int len = Original_MAC_recvMsg(h, extendedData);
    if (len <= 0)
    {
        return len;
    }
    uint8_t *temp = extendedData;
    uint8_t dest = h->recvH.dst_addr;
    // if (dest != ADDR_BROADCAST) // exclude broadcasts - beacons
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
        // if (overhead && isMsg)
        if (overhead)
        {
            uint8_t src = h->recvH.src_addr;
            // extract hop timestamp
            time_t mac_ts;
            memcpy(&mac_ts, temp, sizeof(mac_ts));
            temp += sizeof(mac_ts);
            unsigned int latency = (time(NULL) - mac_ts);
            if (config.loglevel >= DEBUG)
            {
                printf("ProtoMon : hop src:%02d latency:%ds\n", src, latency);
            }

            // Capture metrics
            sem_wait(&macMetrics.mutex);
            if (src > macMetrics.maxAddr)
            {
                macMetrics.maxAddr = src;
            }
            if (src < macMetrics.minAddr)
            {
                macMetrics.minAddr = src;
            }
            macMetrics.data[src].recv++;
            macMetrics.data[src].latency += latency;
            sem_post(&macMetrics.mutex);
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

    return len - overhead;
}

int ProtoMon_MAC_timedRecv(MAC *h, unsigned char *data, unsigned int timeout)
{
    time_t start = time(NULL);
    uint16_t overhead = getMACOverhead();
    uint8_t extendedData[MAX_PAYLOAD_SIZE];
    int len = Original_MAC_timedRecvMsg(h, extendedData, timeout);
    if (len <= 0)
    {
        return len;
    }
    uint8_t *temp = extendedData;
    uint8_t dest = h->recvH.dst_addr;
    // if (dest != ADDR_BROADCAST) // exclude broadcasts - beacons
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
        // if (overhead && isMsg)
        if (overhead)
        {
            uint8_t src = h->recvH.src_addr;
            // extract hop timestamp
            time_t mac_ts;
            memcpy(&mac_ts, temp, sizeof(mac_ts));
            temp += sizeof(mac_ts);
            unsigned int latency = (time(NULL) - mac_ts);
            if (config.loglevel >= DEBUG)
            {
                printf("ProtoMon : hop src:%02d latency:%ds\n", src, latency);
            }

            // Capture metrics
            sem_wait(&macMetrics.mutex);
            if (src > macMetrics.maxAddr)
            {
                macMetrics.maxAddr = src;
            }
            if (src < macMetrics.minAddr)
            {
                macMetrics.minAddr = src;
            }
            macMetrics.data[src].recv++;
            macMetrics.data[src].latency += latency;
            sem_post(&macMetrics.mutex);
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
    return len - overhead;
}

static int writeBufferToFile(const uint8_t *fileName, uint8_t *temp)
{
    if (config.loglevel >= DEBUG)
    {
        printf("# %s: %ld\n%s\n", fileName, strlen(temp), temp);
    }
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

// void ProtoMon_setOrigRSend(int (*func)(uint8_t, uint8_t *, unsigned int))
// {
//     if (Original_Routing_sendMsg == NULL)
//     {
//         Original_Routing_sendMsg = func;
//     }
// }

// void ProtoMon_setOrigRRecv(int (*func)(Routing_Header *, uint8_t *))
// {
//     if (Original_Routing_recvMsg == NULL)
//     {
//         Original_Routing_recvMsg = func;
//     }
// }

// void ProtoMon_setOrigRTimedRecv(int (*func)(Routing_Header *, uint8_t *, unsigned int))
// {
//     if (Original_Routing_timedRecvMsg == NULL)
//     {
//         Original_Routing_timedRecvMsg = func;
//     }
// }

// void ProtoMon_setOrigMACSend(int (*func)(MAC *, unsigned char, unsigned char *, unsigned int))
// {
//     if (Original_MAC_sendMsg == NULL)
//     {
//         Original_MAC_sendMsg = func;
//     }
// }

// void ProtoMon_setOrigMACRecv(int (*func)(MAC *, unsigned char *))
// {
//     if (Original_MAC_recvMsg == NULL)
//     {
//         Original_MAC_recvMsg = func;
//     }
// }

// void ProtoMon_setOrigMACTimedRecv(int (*func)(MAC *, unsigned char *, unsigned int))
// {
//     if (Original_MAC_timedRecvMsg == NULL)
//     {
//         Original_MAC_timedRecvMsg = func;
//     }
// }
