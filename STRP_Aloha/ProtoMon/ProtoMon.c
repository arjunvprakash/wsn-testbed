#include "ProtoMon.h"

#include <stdio.h>     // printf
#include <stdlib.h>    // rand, malloc, free, exit
#include <unistd.h>    // sleep, exec, chdir
#include <string.h>    // memcpy, strerror, strrok
#include <pthread.h>   // pthread_create
#include <time.h>      // time
#include <errno.h>     // errno
#include <signal.h>    // signal
#include <semaphore.h> // sem_init, sem_wait, sem_trywait, sem_timedwait

#include "../common.h"
#include "../util.h"
#include "metric.h"

#define HTTP_PORT 8000
#define SINK_MAX_BUFFER 1024

typedef enum
{
    CTRL_MSG = '\x71',
    CTRL_TAB = '\x72',
    CTRL_MAC = '\x73',
    CTRL_ROU = '\x78',
    CTRL_MET = '\x79'
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

typedef struct MACMetrics
{
    MAC_Data data[MAX_ACTIVE_NODES];
    uint8_t minAddr, maxAddr;
    sem_t mutex;
} MACMetrics;

// Define routing parameters
#define MAX_ROUTING_PARAMS 5
typedef enum
{
    NUMHOPS,
    SENT,
    RECV,
    LATENCY,
    PATH
} RoutingParamIndex;

Parameter routingParams[] = {
    {.name = "NumHops", .type = TYPE_UINT8},     // Number of hops
    {.name = "TotalSent", .type = TYPE_UINT16},  // Total packets sent
    {.name = "TotalRecv", .type = TYPE_UINT16},  // Total packets received
    {.name = "AvgLatency", .type = TYPE_UINT16}, // Average latency
    {.name = "Path", .type = TYPE_UINT8}         // Path information
};

uint8_t numRoutingParams = sizeof(routingParams) / sizeof(Parameter);

Parameter macParams[] = {
    {.name = "TotalSent", .type = TYPE_UINT16}, // Total packets sent
    {.name = "TotalRecv", .type = TYPE_UINT16}, // Total packets received
    {.name = "AvgLatency", .type = TYPE_UINT16} // Average latency
};
uint8_t numMacParams = sizeof(macParams) / sizeof(Parameter);

Metric routingValues[MAX_ACTIVE_NODES];
Metric macValues[MAX_ACTIVE_NODES];

typedef struct Routing_Data
{
    uint16_t numHops;
    uint16_t sent;
    uint16_t recv;
    uint16_t totalLatency;
    uint8_t path[240];
} Routing_Data;

typedef struct RoutingMetrics
{
    Routing_Data data[MAX_ACTIVE_NODES];
    uint8_t minAddr, maxAddr;
    sem_t mutex;
} RoutingMetrics;

void initRoutingMetricsV2()
{
    for (int i = 0; i < MAX_ACTIVE_NODES; i++)
    {
        Metric_init(&routingValues[i], i, routingParams, numRoutingParams);
    }
}

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
static const char pathSeparator = '-'; // DO NOT use comma

static ProtoMon_Config config;
static MACMetrics macMetrics;
static RoutingMetrics routingMetrics;
static time_t startTime, lastVizTime, lastMacWrite, lastNeighborWrite, lastRoutingWrite;
static uint8_t lastPath[240];

static int ProtoMon_Routing_sendMsg(uint8_t dest, uint8_t *data, unsigned int len);
static int ProtoMon_Routing_recvMsg(Routing_Header *h, uint8_t *data);
static int ProtoMon_Routing_timedRecvMsg(Routing_Header *header, uint8_t *data, unsigned int timeout);

static int ProtoMon_Routing_timedRecvMsgV2(Routing_Header *header, uint8_t *data, unsigned int timeout);

static int ProtoMon_MAC_send(MAC *h, unsigned char dest, unsigned char *data, unsigned int len);
static int ProtoMon_MAC_recv(MAC *h, unsigned char *data);
static int ProtoMon_MAC_timedRecv(MAC *h, unsigned char *data, unsigned int timeout);

static int killProcessOnPort(int port);
static void installDependencies();
static void initOutputFiles();
static void generateGraph();
static void createHttpServer(int port);
static void *sendMetrics_func(void *args);
static int writeBufferToFile(const uint8_t *fileName, uint8_t *temp);
static int sendMetricsToSink(uint8_t *buffer, unsigned int len, CTRL ctrl);
static uint16_t getMetricsCSV(uint8_t *buffer, uint16_t bufferSize, CTRL ctrl);
static uint16_t getRoutingOverhead();
static uint16_t getMACOverhead();
static void initMetrics();
static void resetMacMetrics();
static void resetRoutingMetrics();
static void signalHandler(int signum);
void deserializeMetricsPkt(uint8_t *buffer, uint16_t bufferSize, uint8_t src, CTRL ctrl);

// static void initOutputFilesV2()
// {
//     // Create results dir
//     char cmd[150];
//     sprintf(cmd, "[ -d '%s' ] || mkdir -p '%s' && cp '../ProtoMon/viz/index.html' '%s/index.html'", outputDir, outputDir, outputDir);
//     if (system(cmd) != 0)
//     {
//         logMessage(ERROR, "%s - Error creating results dir\n", __func__);
//         fflush(stdout);
//         exit(EXIT_FAILURE);
//     }

//     // Create mac.csv
//     if (config.monitoredLevels & PROTOMON_LEVEL_MAC)
//     {
//         char filePath[100];
//         sprintf(filePath, "%s/%s", outputDir, macCSV);
//         FILE *file = fopen(filePath, "w");
//         if (file == NULL)
//         {
//             logMessage(ERROR, "%s - Error creating %s file:\n", __func__, macCSV);
//             fflush(stdout);
//             exit(EXIT_FAILURE);
//         }
//         const char *header = "Timestamp,Source,Address,TotalSent,TotalRecv,AvgLatency";
//         fprintf(file, "%s", header);
//         uint8_t *extra = MAC_getMetricsHeader();
//         if (strlen(extra))
//         {
//             fprintf(file, ",%s", extra);
//         }
//         fprintf(file, "\n");
//         fflush(file);
//         fclose(file);
//         if (config.loglevel >= DEBUG)
//         {
//             logMessage(DEBUG, "CSV file: %s created\n", macCSV);
//         }
//     }

//     // Create network.csv
//     if (config.monitoredLevels & PROTOMON_LEVEL_TOPO)
//     {
//         char filePath[100];
//         sprintf(filePath, "%s/%s", outputDir, networkCSV);
//         FILE *file = fopen(filePath, "w");
//         if (file == NULL)
//         {
//             logMessage(ERROR, "%s - Error creating %s file:\n", __func__, networkCSV);
//             fflush(stdout);
//             exit(EXIT_FAILURE);
//         }
//         // Write the header for the network topology
//         const char *nwheader = "Timestamp,Source";
//         fprintf(file, "%s", nwheader);
//         Metric nwMetric = Routing_GetTopologyDataV2(0);
//         for (int i = 0; i < nwMetric.numParams; i++)
//         {
//             fprintf(file, ",%s", nwMetric.params[i].name);
//         }
//         fprintf(file, "\n");
//         fflush(file);

//         // const char *nwheader = Routing_getTopologyHeader();
//         // fprintf(file, "%s", nwheader);
//         // fprintf(file, "\n");
//         // fflush(file);

//         fclose(file);
//         if (config.loglevel >= DEBUG)
//         {
//             logMessage(DEBUG, "CSV file: %s created\n", networkCSV);
//         }
//     }

//     // Create routing.csv
//     if (config.monitoredLevels & PROTOMON_LEVEL_ROUTING)
//     {
//         char filePath[100];
//         sprintf(filePath, "%s/%s", outputDir, routingCSV);
//         FILE *file = fopen(filePath, "w");
//         if (file == NULL)
//         {
//             logMessage(ERROR, "Error creating %s file: %s\n", routingCSV, __func__);
//             fflush(stdout);
//             exit(EXIT_FAILURE);
//         }

//         const char *header = "Timestamp,Source,Address,TotalSent,TotalRecv,NumHops,AvgLatency";
//         fprintf(file, "%s", header);
//         Metric routingMetric = Routing_GetMetricsDataV2(0);
//         for (int i = 0; i < routingMetric.numParams; i++)
//         {
//             fprintf(file, ",%s", routingMetric.params[i].name);
//         }
//         fprintf(file, ",Path\n");
//         fflush(file);
//         fclose(file);

//         // const char *header = "Timestamp,Source,Address,TotalSent,TotalRecv,NumHops,AvgLatency";
//         // fprintf(file, "%s", header);
//         // uint8_t *extra = Routing_getMetricsHeader();
//         // if (strlen(extra))
//         // {
//         //     fprintf(file, ",%s", extra);
//         // }
//         // fprintf(file, ",Path");
//         // fprintf(file, "\n");
//         // fflush(file);
//         // fclose(file);

//         if (config.loglevel >= DEBUG)
//         {
//             logMessage(DEBUG, "CSV file: %s created\n", routingCSV);
//         }
//     }
// }

static void initOutputFiles()
{
    // Create results dir
    char cmd[150];
    sprintf(cmd, "[ -d '%s' ] || mkdir -p '%s' && cp '../ProtoMon/viz/index.html' '%s/index.html'", outputDir, outputDir, outputDir);
    if (system(cmd) != 0)
    {
        logMessage(ERROR, "%s - Error creating results dir\n", __func__);
        fflush(stdout);
        exit(EXIT_FAILURE);
    }

    // Create mac.csv
    if (config.monitoredLevels & PROTOMON_LEVEL_MAC)
    {
        char filePath[100];
        sprintf(filePath, "%s/%s", outputDir, macCSV);
        FILE *file = fopen(filePath, "w");
        if (file == NULL)
        {
            logMessage(ERROR, "%s - Error creating %s file:\n", __func__, macCSV);
            fflush(stdout);
            exit(EXIT_FAILURE);
        }
        const char *header = "Timestamp,Source,Address,TotalSent,TotalRecv,AvgLatency";
        fprintf(file, "%s", header);
        uint8_t *extra = MAC_getMetricsHeader();
        if (strlen(extra))
        {
            fprintf(file, ",%s", extra);
        }
        fprintf(file, "\n");
        fflush(file);
        fclose(file);
        if (config.loglevel >= DEBUG)
        {
            logMessage(DEBUG, "CSV file: %s created\n", macCSV);
        }
    }

    // Create network.csv
    if (config.monitoredLevels & PROTOMON_LEVEL_TOPO)
    {
        char filePath[100];
        sprintf(filePath, "%s/%s", outputDir, networkCSV);
        FILE *file = fopen(filePath, "w");
        if (file == NULL)
        {
            logMessage(ERROR, "%s - Error creating %s file:\n", __func__, networkCSV);
            fflush(stdout);
            exit(EXIT_FAILURE);
        }
        const char *nwheader = Routing_getTopologyHeader();
        fprintf(file, "%s", nwheader);
        fprintf(file, "\n");
        fflush(file);
        fclose(file);
        if (config.loglevel >= DEBUG)
        {
            logMessage(DEBUG, "CSV file: %s created\n", networkCSV);
        }
    }

    // Create routing.csv
    if (config.monitoredLevels & PROTOMON_LEVEL_ROUTING)
    {
        char filePath[100];
        sprintf(filePath, "%s/%s", outputDir, routingCSV);
        FILE *file = fopen(filePath, "w");
        if (file == NULL)
        {
            logMessage(ERROR, "Error creating %s file: %s\n", routingCSV, __func__);
            fflush(stdout);
            exit(EXIT_FAILURE);
        }
        const char *header = "Timestamp,Source,Address,TotalSent,TotalRecv,NumHops,AvgLatency";
        fprintf(file, "%s", header);
        uint8_t *extra = Routing_getMetricsHeader();
        if (strlen(extra))
        {
            fprintf(file, ",%s", extra);
        }
        fprintf(file, ",Path");
        fprintf(file, "\n");
        fflush(file);
        fclose(file);
        if (config.loglevel >= DEBUG)
        {
            logMessage(DEBUG, "CSV file: %s created\n", routingCSV);
        }
    }
}

static void generateGraph()
{
    char cmd[100];
    sprintf(cmd, "python ../../ProtoMon/viz/script.py %d&", ADDR_SINK);
    if (system(cmd) != 0)
    {
        logMessage(ERROR, "Error generating graph\n");
    }
    else
    {
        logMessage(INFO, "Visualising metrics. Open http://localhost:8000\n");
    }
    fflush(stdout);
}

static void installDependencies()
{

    if (config.loglevel >= DEBUG)
    {
        logMessage(DEBUG, "Installing dependencies...\n");
    }
    char *setenv_cmd = "pip install -r ../ProtoMon/viz/requirements.txt > /dev/null &";
    if (config.loglevel == TRACE)
    {
        logMessage(TRACE, "Executing command : %s\n", setenv_cmd);
    }
    if (system(setenv_cmd) != 0)
    {
        logMessage(ERROR, "Error installing dependencies. Exiting...\n");
        fflush(stdout);
        exit(EXIT_FAILURE);
    }
    if (config.loglevel >= DEBUG)
    {
        logMessage(DEBUG, "Successfully installed dependencies...\n");
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
        logMessage(DEBUG, "Port %d freed\n", port);
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
        logMessage(ERROR, "Error starting HTTP server\n");
        fflush(stdout);
        exit(EXIT_FAILURE);
    }

    if (config.loglevel >= DEBUG)
    {
        logMessage(DEBUG, "HTTP server started on port: %d\n", port);
        fflush(stdout);
    }
}

static int sendMetricsToSinkV2(uint8_t *buffer, uint16_t len, uint8_t metricsCount)
{
    const unsigned int extLen = len + sizeof(uint8_t) + sizeof(uint16_t);
    uint8_t extBuffer[extLen];

    // Set control flag: MET
    uint8_t *temp = extBuffer;
    uint8_t ctrlFlag = CTRL_MET;
    memcpy(temp, &ctrlFlag, sizeof(ctrlFlag));
    temp += sizeof(ctrlFlag);

    memcpy(temp, &metricsCount, sizeof(metricsCount));
    temp += sizeof(metricsCount);

    memcpy(temp, buffer, len);
    return Original_Routing_sendMsg(ADDR_SINK, extBuffer, extLen);
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

static uint16_t getMetricsBufferV1(uint8_t *buffer, uint16_t bufferSize, CTRL ctrl)
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
                memset(row, 0, sizeof(row));
                uint8_t extra[50];
                memset(extra, 0, sizeof(extra));
                int extraLen = MAC_getMetricsData(extra, i);
                int rowLen = snprintf(row + strlen(row), sizeof(row) - strlen(row), "%ld,%d,%d,%d,%d,%ld", (unsigned long)timestamp, config.self, i, data.sent, data.recv, data.recv > 0 ? (unsigned long)(data.latency / data.recv) : 0);
                if (extraLen)
                {
                    rowLen += snprintf(row + strlen(row), sizeof(row) - strlen(row), ",%s", extra);
                }
                rowLen += snprintf(row + strlen(row), sizeof(row) - strlen(row), "\n");

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
                        logMessage(DEBUG, "MAC metrics buffer overflow\n");
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
                memset(row, 0, sizeof(row));
                uint8_t extra[50];
                memset(extra, 0, sizeof(extra));
                int extraLen = Routing_getMetricsData(extra, i);
                int rowLen = snprintf(row + strlen(row), sizeof(row) - strlen(row), "%ld,%d,%d,%d,%d,%d,%ld", (unsigned long)timestamp, config.self, i, data.sent, data.recv, data.numHops, data.recv > 0 ? (unsigned long)(data.totalLatency / data.recv) : 0);
                if (extraLen)
                {
                    rowLen += snprintf(row + strlen(row), sizeof(row) - strlen(row), ",%s", extra);
                }
                uint8_t path[MAX_PAYLOAD_SIZE];
                if (strlen(data.path))
                {
                    strcpy(path, data.path);
                }
                else
                {
                    strcpy(path, "");
                }
                rowLen += snprintf(row + strlen(row), sizeof(row) - strlen(row), ",%s", strlen(data.path) ? data.path : (uint8_t *)"");
                rowLen += snprintf(row + strlen(row), sizeof(row) - strlen(row), "\n");

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
                        logMessage(DEBUG, "Routing metrics buffer overflow\n");
                    }
                    break;
                }
            }
        }
    }
    else if (ctrl == CTRL_TAB)
    {
        usedSize += Routing_getTopologyData(buffer, bufferSize);
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

static uint16_t getMetricsBufferV2(uint8_t *buffer, uint16_t bufferSize, uint8_t *metricsCount)
{
    uint16_t usedSize = 0;
    *metricsCount = 0;
    uint8_t metricsPayloadHeader = sizeof(uint8_t) + sizeof(uint16_t);
    const uint16_t rowLen = (uint16_t)MAX_PAYLOAD_SIZE / 2;

    if (config.monitoredLevels | PROTOMON_LEVEL_MAC)
    {
        uint16_t offset = metricsPayloadHeader;
        MACMetrics metrics = macMetrics;

        // Reset metrics
        resetMacMetrics();
        time_t timestamp = time(NULL);

        for (uint8_t i = metrics.minAddr; i <= metrics.maxAddr; i++)
        {
            // Generate CSV row for each non zero node
            const MAC_Data data = metrics.data[i];
            if (data.sent > 0 || data.recv > 0)
            {
                uint8_t row[rowLen];
                memset(row, 0, sizeof(row));
                uint8_t extra[rowLen];
                memset(extra, 0, sizeof(extra));
                int extraLen = MAC_getMetricsData(extra, i);
                int rowLen = snprintf(row + strlen(row), sizeof(row) - strlen(row), "%ld,%d,%d,%d,%d,%ld", (unsigned long)timestamp, config.self, i, data.sent, data.recv, data.recv > 0 ? (unsigned long)(data.latency / data.recv) : 0);
                if (extraLen)
                {
                    rowLen += snprintf(row + strlen(row), sizeof(row) - strlen(row), ",%s", extra);
                }
                rowLen += snprintf(row + strlen(row), sizeof(row) - strlen(row), "\n");

                // clearing the timestamp to save packet size
                timestamp = 0L;

                if (rowLen < (bufferSize - (usedSize + offset)))
                {
                    memcpy(buffer + (usedSize + offset), row, rowLen);
                    offset += rowLen;
                }
                else
                {
                    // if (config.loglevel >= DEBUG)
                    {
                        logMessage(ERROR, "MAC metrics buffer overflow\n");
                        fflush(stdout);
                        exit(EXIT_FAILURE);
                    }
                    break;
                }
            }
        }

        if (offset > metricsPayloadHeader)
        {
            buffer[usedSize] = CTRL_MAC;
            usedSize++;
            offset -= (metricsPayloadHeader);
            memcpy(buffer + usedSize, &offset, sizeof(offset));
            usedSize += offset + sizeof(offset);
            *metricsCount += 1;
        }
    }

    if (config.monitoredLevels | PROTOMON_LEVEL_ROUTING)
    {
        uint16_t offset = metricsPayloadHeader;
        RoutingMetrics metrics = routingMetrics;

        // Reset metrics
        resetRoutingMetrics();
        time_t timestamp = time(NULL);

        for (uint8_t i = metrics.minAddr; i <= metrics.maxAddr; i++)
        {
            const Routing_Data data = metrics.data[i];
            // Generate CSV row for each non zero node
            if (data.sent > 0 || data.recv > 0)
            {
                uint8_t row[rowLen];
                memset(row, 0, sizeof(row));
                uint8_t extra[rowLen];
                memset(extra, 0, sizeof(extra));
                int extraLen = Routing_getMetricsData(extra, i);
                int rowLen = snprintf(row + strlen(row), sizeof(row) - strlen(row), "%ld,%d,%d,%d,%d,%d,%ld", (unsigned long)timestamp, config.self, i, data.sent, data.recv, data.numHops, data.recv > 0 ? (unsigned long)(data.totalLatency / data.recv) : 0);
                if (extraLen)
                {
                    rowLen += snprintf(row + strlen(row), sizeof(row) - strlen(row), ",%s", extra);
                }
                uint8_t path[MAX_PAYLOAD_SIZE];
                if (strlen(data.path))
                {
                    strcpy(path, data.path);
                }
                else
                {
                    strcpy(path, "");
                }
                rowLen += snprintf(row + strlen(row), sizeof(row) - strlen(row), ",%s", strlen(data.path) ? data.path : (uint8_t *)"");
                rowLen += snprintf(row + strlen(row), sizeof(row) - strlen(row), "\n");

                // clearing the timestamp to save packet size
                timestamp = 0L;

                if (rowLen < (bufferSize - (usedSize + offset)))
                {
                    memcpy(buffer + (usedSize + offset), row, rowLen);
                    offset += rowLen;
                }
                else
                {
                    // if (config.loglevel >= DEBUG)
                    {
                        logMessage(ERROR, "Routing metrics buffer overflow\n");
                        fflush(stdout);
                        exit(EXIT_FAILURE);
                    }
                    break;
                }
            }
        }

        if (offset > metricsPayloadHeader)
        {
            buffer[usedSize] = CTRL_ROU;
            usedSize++;
            offset -= (metricsPayloadHeader);
            memcpy(buffer + usedSize, &offset, sizeof(offset));
            usedSize += offset + sizeof(offset);
            *metricsCount += 1;
        }
    }

    if (config.monitoredLevels | PROTOMON_LEVEL_TOPO)
    {
        uint16_t offset = metricsPayloadHeader;

        offset += Routing_getTopologyData(buffer + (usedSize + offset), bufferSize - (usedSize + offset));

        if (offset > metricsPayloadHeader)
        {
            buffer[usedSize] = CTRL_TAB;
            usedSize++;
            offset -= (metricsPayloadHeader);
            memcpy(buffer + usedSize, &offset, sizeof(offset));
            usedSize += offset + sizeof(offset);
            *metricsCount += 1;
        }
    }

    if (config.loglevel > DEBUG && usedSize > 1)
    {
        logMessage(DEBUG, "Metrics (%d):\n", usedSize);
        for (int i = 0; i < usedSize; i++)
        {
            printf("%02X ", buffer[i]);
        }
        printf("\n");
    }
    return usedSize > 1 ? usedSize : 0;
}

void deserializeMetricsPkt(uint8_t *buffer, uint16_t bufferSize, uint8_t src, CTRL ctrl)
{
    if (bufferSize < sizeof(uint8_t) + sizeof(uint16_t))
    {
        logMessage(ERROR, "Buffer size too small for deserialization\n");
        return;
    }

    uint8_t *p = buffer;

    uint8_t ctrlFlag;
    memcpy(&ctrlFlag, p, sizeof(ctrlFlag));
    p += sizeof(ctrlFlag);
    if (ctrlFlag != ctrl)
    {
        logMessage(ERROR, "Control flag mismatch: expected %d, got %d\n", ctrl, ctrlFlag);
        return;
    }

    if (config.loglevel >= TRACE)
    {
        printf("# ");
        for (int i = 0; i < bufferSize - sizeof(ctrlFlag); i++)
        {
            printf("%02X ", p[i]);
        }
        printf("\n");
    }

    uint8_t numRows;
    memcpy(&numRows, p, sizeof(numRows));
    p += sizeof(numRows);

    time_t timestamp;
    memcpy(&timestamp, p, sizeof(timestamp));
    p += sizeof(timestamp);

    if (numRows == 0)
    {
        logMessage(ERROR, "No rows in %s packet\n", ctrl == CTRL_MAC ? "MAC" : (ctrl == CTRL_ROU ? "Routing" : "Topology"));
        return;
    }

    const uint8_t *fileName = ctrl == CTRL_MAC ? macCSV : (ctrl == CTRL_ROU ? routingCSV : networkCSV);

    if (ctrl == CTRL_ROU)
    {
        for (int i = 0; i < numRows; i++)
        {
            uint8_t csvRow[SINK_MAX_BUFFER];
            uint8_t addr;
            memcpy(&addr, p, sizeof(addr));
            p += sizeof(addr);
            int rowLen = snprintf(csvRow, SINK_MAX_BUFFER, "%ld,%d,%d", (long)timestamp, src, addr);

            for (int j = 0; j < routingValues[0].numParams; j++)
            {
                Param_Type type = routingValues[0].params[j].type;
                if (j == PATH)
                {
                    // Handle path separately
                    uint8_t *path = p;
                    size_t pathLen = strlen(path);
                    if (pathLen > 0)
                    {
                        rowLen += snprintf(csvRow + rowLen, SINK_MAX_BUFFER - rowLen, ",%s", path);
                    }
                    p += pathLen + 1; // move past null terminator
                    continue;
                }

                switch (type)
                {
                case TYPE_UINT8:
                {
                    uint8_t value;
                    memcpy(&value, p, sizeof(value));
                    p += sizeof(value);
                    rowLen += snprintf(csvRow + rowLen, SINK_MAX_BUFFER - rowLen, ",%d", value);
                    break;
                }
                case TYPE_UINT16:
                {
                    uint16_t value;
                    memcpy(&value, p, sizeof(value));
                    p += sizeof(value);
                    rowLen += snprintf(csvRow + rowLen, SINK_MAX_BUFFER - rowLen, ",%d", value);
                    break;
                }
                case TYPE_INT16:
                {
                    int16_t value;
                    memcpy(&value, p, sizeof(value));
                    p += sizeof(value);
                    rowLen += snprintf(csvRow + rowLen, SINK_MAX_BUFFER - rowLen, ",%d", value);
                    break;
                }
                case TYPE_INT8:
                {
                    int8_t value;
                    memcpy(&value, p, sizeof(value));
                    p += sizeof(value);
                    rowLen += snprintf(csvRow + rowLen, SINK_MAX_BUFFER - rowLen, ",%d", value);
                    break;
                }
                case TYPE_FLOAT:
                {
                    float value;
                    memcpy(&value, p, sizeof(value));
                    rowLen += snprintf(csvRow + rowLen, SINK_MAX_BUFFER - rowLen, ",%.2f", value);
                    p += sizeof(value);
                    break;
                }
                case TYPE_INT:
                {
                    int value;
                    memcpy(&value, p, sizeof(value));
                    p += sizeof(value);
                    rowLen += snprintf(csvRow + rowLen, SINK_MAX_BUFFER - rowLen, ",%d", value);
                    break;
                }
                default:
                    logMessage(ERROR, "Unknown parameter type: %d\n", type);
                }
            }
            csvRow[rowLen++] = '\n'; // newline
            csvRow[rowLen] = '\0';   // null terminatior
            logMessage(DEBUG, "%s", csvRow);
            writeBufferToFile(fileName, csvRow);
        }
    }
}

static uint16_t getMetricsSerialized(uint8_t *buffer, uint16_t bufferSize, CTRL ctrl)
{
    uint16_t usedSize = 0;
    time_t timestamp = time(NULL);

    if (ctrl == CTRL_ROU)
    {
        int metricSize = Metric_getSize(routingValues[0]);

        // reserve space for numMetrics
        uint8_t numMetrics = 0;
        uint8_t *nuMetricsPtr = (uint8_t *)(buffer + usedSize);
        usedSize += sizeof(numMetrics);

        // reserve space for timestamp
        uint8_t *timestampPtr = (uint8_t *)(buffer + usedSize);
        usedSize += sizeof(timestamp);

        for (uint8_t i = 1; i < MAX_ACTIVE_NODES; i++)
        {
            Metric metric = routingValues[i];
            if (metric.params == NULL || metric.numParams == 0)
            {
                continue;
            }
            if (usedSize + metricSize >= bufferSize)
            {
                if (config.loglevel >= DEBUG)
                {
                    logMessage(DEBUG, "Routing metrics buffer overflow\n");
                }
                break;
            }
            if (metric.params[SENT].value != NULL || metric.params[RECV].value != NULL)
            {
                numMetrics++;
                memcpy(buffer + usedSize, &metric.addr, sizeof(metric.addr));
                usedSize += sizeof(metric.addr);

                // Ensure order of parameters is same as in definition

                uint8_t numHops = metric.params[NUMHOPS].value ? *(uint8_t *)metric.params[NUMHOPS].value : 0;
                memcpy(buffer + usedSize, &numHops, sizeof(numHops));
                usedSize += sizeof(numHops);

                uint16_t sent = metric.params[SENT].value ? *(uint16_t *)metric.params[SENT].value : 0;
                memcpy(buffer + usedSize, &sent, sizeof(sent));
                usedSize += sizeof(sent);

                uint16_t recv = metric.params[RECV].value ? *(uint16_t *)metric.params[RECV].value : 0;
                memcpy(buffer + usedSize, &recv, sizeof(recv));
                usedSize += sizeof(recv);

                uint16_t latency = metric.params[LATENCY].value ? *(uint16_t *)metric.params[LATENCY].value : 0;
                memcpy(buffer + usedSize, &latency, sizeof(latency));
                usedSize += sizeof(latency);

                uint8_t *path = metric.params[PATH].value ? (uint8_t *)metric.params[PATH].value : (uint8_t *)"";
                size_t pathLen = strlen((char *)path);
                if (pathLen > 0)
                {
                    memcpy(buffer + usedSize, path, pathLen);
                    usedSize += pathLen;
                }

                buffer[usedSize++] = '\0'; // null-terminate path

                // reset routing metrics
                Metric_reset(&metric);
            }
        }
        if (numMetrics > 0)
        {
            memcpy(nuMetricsPtr, &numMetrics, sizeof(numMetrics));
            memcpy(timestampPtr, &timestamp, sizeof(timestamp));
        }
        else
        {
            // No metrics to send, reset usedSize
            usedSize = 0;
        }
    }
    printf("%s buffer size: %d B\n", ctrl == CTRL_MAC ? "MAC" : (ctrl == CTRL_ROU ? "Routing" : "Topology"), usedSize);
    for (int i = 0; i < usedSize; i++)
    {
        // if (config.loglevel > DEBUG)
        {
            printf("%02X ", buffer[i]);
        }
    }
    printf("\n");
    return usedSize > 1 ? usedSize : 0;
}

uint16_t getMetricsCSV2(uint8_t *buffer, uint16_t bufferSize, CTRL ctrl)
{
    uint16_t usedSize = 0;
    time_t timestamp = time(NULL);
    if (ctrl == CTRL_ROU)
    {
        for (uint8_t i = 1; i < MAX_ACTIVE_NODES / 4; i++)
        {
            Metric metric = routingValues[i];
            if (metric.params == NULL || metric.numParams == 0)
            {
                continue;
            }
            if (usedSize >= bufferSize)
            {
                if (config.loglevel >= DEBUG)
                {
                    logMessage(DEBUG, "Routing metrics buffer overflow\n");
                }
                break;
            }
            if (metric.params[SENT].value != NULL || metric.params[RECV].value != NULL)
            {
                usedSize += snprintf(buffer + usedSize, bufferSize - usedSize, "%ld,%d,%d", (long)timestamp, config.self, metric.addr);

                // Ensure order of parameters is same as in definition

                uint8_t numHops = metric.params[NUMHOPS].value ? *(uint8_t *)metric.params[NUMHOPS].value : 0;
                usedSize += snprintf(buffer + usedSize, bufferSize - usedSize, ",%d", numHops);

                uint16_t sent = metric.params[SENT].value ? *(uint16_t *)metric.params[SENT].value : 0;
                snprintf(buffer + usedSize, bufferSize - usedSize, ",%d", sent);
                usedSize += strlen(buffer + usedSize);

                uint16_t recv = metric.params[RECV].value ? *(uint16_t *)metric.params[RECV].value : 0;
                snprintf(buffer + usedSize, bufferSize - usedSize, ",%d", recv);
                usedSize += strlen(buffer + usedSize);

                uint16_t latency = metric.params[LATENCY].value ? *(uint16_t *)metric.params[LATENCY].value : 0;
                usedSize += snprintf(buffer + usedSize, bufferSize - usedSize, ",%d", latency);

                uint8_t *path = metric.params[PATH].value ? (uint8_t *)metric.params[PATH].value : (uint8_t *)"";
                size_t pathLen = strlen((char *)path);
                if (pathLen > 0)
                {
                    snprintf(buffer + usedSize, bufferSize - usedSize, ",%s", path);
                    usedSize += pathLen;
                }

                buffer[usedSize++] = '\n'; // newline

                // reset metrics
                Metric_reset(&metric);
            }
        }
    }
    if (config.loglevel >= DEBUG)
    {
        logMessage(DEBUG, "Sink %s metrics CSV : %d B\n%s", ctrl == CTRL_ROU ? "Routing" : (ctrl == CTRL_MAC ? "MAC" : "Topology"), usedSize, buffer);
    }
    return usedSize;
}

static uint16_t getMetricsCSV(uint8_t *buffer, uint16_t bufferSize, CTRL ctrl)
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
                memset(row, 0, sizeof(row));
                uint8_t extra[50];
                memset(extra, 0, sizeof(extra));
                int extraLen = MAC_getMetricsData(extra, i);
                int rowLen = snprintf(row + strlen(row), sizeof(row) - strlen(row), "%ld,%d,%d,%d,%d,%ld", (unsigned long)timestamp, config.self, i, data.sent, data.recv, data.recv > 0 ? (unsigned long)(data.latency / data.recv) : 0);
                if (extraLen)
                {
                    rowLen += snprintf(row + strlen(row), sizeof(row) - strlen(row), ",%s", extra);
                }
                rowLen += snprintf(row + strlen(row), sizeof(row) - strlen(row), "\n");

                // clearing the timestamp to save packet size
                timestamp = 0L;

                if (usedSize + rowLen < bufferSize)
                {
                    memcpy(buffer + usedSize, row, rowLen);
                    usedSize += rowLen;
                }
                else
                {
                    // if (config.loglevel >= DEBUG)
                    {
                        logMessage(DEBUG, "MAC metrics buffer overflow\n");
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
        // for (uint8_t i = 0; i < MAX_ACTIVE_NODES; i++)
        {
            const Routing_Data data = metrics.data[i];
            // Generate CSV row for each non zero node
            if (data.sent > 0 || data.recv > 0)
            {
                uint8_t row[150];
                memset(row, 0, sizeof(row));
                uint8_t extra[50];
                memset(extra, 0, sizeof(extra));
                int extraLen = Routing_getMetricsData(extra, i);
                int rowLen = snprintf(row + strlen(row), sizeof(row) - strlen(row), "%ld,%d,%d,%d,%d,%d,%ld", (unsigned long)timestamp, config.self, i, data.sent, data.recv, data.numHops, data.recv > 0 ? (unsigned long)(data.totalLatency / data.recv) : 0);
                if (extraLen)
                {
                    rowLen += snprintf(row + strlen(row), sizeof(row) - strlen(row), ",%s", extra);
                }
                uint8_t path[MAX_PAYLOAD_SIZE];
                if (strlen(data.path))
                {
                    strcpy(path, data.path);
                }
                else
                {
                    strcpy(path, "");
                }
                rowLen += snprintf(row + strlen(row), sizeof(row) - strlen(row), ",%s", strlen(data.path) ? data.path : (uint8_t *)"");
                rowLen += snprintf(row + strlen(row), sizeof(row) - strlen(row), "\n");

                // clearing the timestamp to save packet size
                timestamp = 0L;

                if (usedSize + rowLen < bufferSize)
                {
                    memcpy(buffer + usedSize, row, rowLen);
                    usedSize += rowLen;
                }
                else
                {
                    // if (config.loglevel >= DEBUG)
                    {
                        logMessage(DEBUG, "Routing metrics buffer overflow\n");
                    }
                    break;
                }
            }
        }
    }
    else if (ctrl == CTRL_TAB)
    {
        usedSize += Routing_getTopologyData(buffer, bufferSize);
    }
    // add terminating null character
    buffer[usedSize] = '\0';
    usedSize++;

    // if (config.loglevel > DEBUG && usedSize > 1)
    {
        printf("# %s: %d\n%s\n", ctrl == CTRL_MAC ? "MAC" : (ctrl == CTRL_ROU ? "Routing" : "Table"), usedSize, buffer);
    }
    return usedSize > 1 ? usedSize : 0;
}

static void *sendMetrics_funcV2(void *args)
{
    sleep(config.initialSendWaitS);
    uint16_t bufferSize = MAX_PAYLOAD_SIZE - (Routing_getHeaderSize() + MAC_getHeaderSize() + getMACOverhead() + sizeof(uint8_t) + sizeof(uint16_t));

    while (1)
    {
        uint8_t *buffer = (uint8_t *)malloc(bufferSize);
        if (buffer == NULL)
        {
            logMessage(ERROR, "Error allocating memory for routing metrics buffer\n");
            fflush(stdout);
            exit(EXIT_FAILURE);
        }

        uint8_t metricsCount;
        // unsigned int randDelay;
        uint16_t bufLen = getMetricsBufferV2(buffer, bufferSize, &metricsCount);
        if (bufLen)
        {
            // randDelay = (unsigned int)randInRange(1000000, 5000000);
            // usleep(randDelay);
            if (!sendMetricsToSinkV2(buffer, bufLen, metricsCount))
            {
                logMessage(ERROR, "Failed to send metrics to sink\n");
            }
            else
            {
                logMessage(INFO, "Sent metrics to %02d : %d B\n", ADDR_SINK, bufLen);
            }
        }
        free(buffer);

        fflush(stdout);

        // sleep(config.sendIntervalS - (unsigned int)(randDelay / 1000000));
        sleep(config.sendIntervalS);
    }
    return NULL;
}

static void *sendMetrics_func(void *args)
{
    sleep(config.initialSendWaitS);
    uint16_t bufferSize = MAX_PAYLOAD_SIZE - (Routing_getHeaderSize() + MAC_getHeaderSize() + getMACOverhead());
    while (1)
    {
        uint16_t totalDelayS;
        uint8_t delayNext;
        // Send routing metrics to sink
        if (config.monitoredLevels & PROTOMON_LEVEL_ROUTING)
        {

            uint8_t *buffer = (uint8_t *)malloc(bufferSize);
            uint16_t bufLen;
            if (buffer == NULL)
            {
                logMessage(ERROR, "Error allocating memory for metrics buffer\n");
                fflush(stdout);
                exit(EXIT_FAILURE);
            }
            if (1)
            {

                bufLen = getMetricsSerialized(buffer, bufferSize, CTRL_ROU);
            }
            else
            {
                bufLen = getMetricsCSV(buffer, bufferSize, CTRL_ROU);
            }
            if (bufLen)
            {
                if (!sendMetricsToSink(buffer, bufLen, CTRL_ROU))
                {
                    logMessage(ERROR, "Failed to send Routing metrics to sink\n");
                    fflush(stdout);
                }
                else
                {
                    logMessage(INFO, "Sent Routing metrics to sink: %d B\n", bufLen);
                    delayNext++;
                }
            }
            free(buffer);
        }

        // Send topology to sink
        if (config.monitoredLevels & PROTOMON_LEVEL_TOPO)
        {
            uint8_t *buffer = (uint8_t *)malloc(bufferSize);
            if (buffer == NULL)
            {
                logMessage(ERROR, "Error allocating memory for topology data buffer\n");
                fflush(stdout);
                exit(EXIT_FAILURE);
            }
            uint16_t bufLen = getMetricsCSV(buffer, bufferSize, CTRL_TAB);
            if (bufLen)
            {
                if (delayNext)
                {
                    delayNext--;
                    sleep(config.sendDelayS);
                    totalDelayS += config.sendDelayS;
                }
                if (!sendMetricsToSink(buffer, bufLen, CTRL_TAB))
                {
                    logMessage(ERROR, "Failed to send Topology data to sink\n");
                    fflush(stdout);
                }
                else
                {
                    logMessage(INFO, "Sent neighbour data to sink: %d B\n", bufLen);
                    delayNext++;
                }
            }
            free(buffer);
        }

        // Send MAC metrics to sink
        if (config.monitoredLevels & PROTOMON_LEVEL_MAC)
        {
            uint8_t buffer[bufferSize];
            if (buffer == NULL)
            {
                logMessage(ERROR, "Error allocating memory for MAC metrics buffer\n");
                fflush(stdout);
                exit(EXIT_FAILURE);
            }
            uint16_t bufLen = getMetricsCSV(buffer, bufferSize, CTRL_MAC);
            if (bufLen)
            {
                if (delayNext)
                {
                    delayNext--;
                    sleep(config.sendDelayS);
                    totalDelayS += config.sendDelayS;
                }
                if (!sendMetricsToSink(buffer, bufLen, CTRL_MAC))
                {
                    logMessage(ERROR, "Failed to send MAC metrics to sink\n");
                    fflush(stdout);
                }
                else
                {
                    logMessage(INFO, "Sent MAC metrics to sink: %d B\n", bufLen);
                    delayNext++;
                }
            }
        }

        sleep(config.sendIntervalS - totalDelayS);
    }
    return NULL;
}

static void signalHandler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM || signum == SIGABRT || signum == SIGSEGV || signum == SIGILL || signum == SIGFPE)
    {
        {
            killProcessOnPort(HTTP_PORT);
            if (config.loglevel >= DEBUG)
            {
                logMessage(DEBUG, "Stopped HTTP server on port %d\n", HTTP_PORT);
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
    if (c->sendDelayS == 0)
    {
        c->sendDelayS = 1;
    }
}

void ProtoMon_init(ProtoMon_Config c)
{
    // Make init idempotent
    if (config.self != 0 || c.monitoredLevels == PROTOMON_LEVEL_NONE)
    {
        return;
    }

    if (c.monitoredLevels != PROTOMON_LEVEL_NONE)
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
        // Routing_timedRecvMsg = &ProtoMon_Routing_timedRecvMsgV2;

        // Must always override MAC functions to increment numHops
        MAC_send = &ProtoMon_MAC_send;
        MAC_recv = &ProtoMon_MAC_recv;
        MAC_timedRecv = &ProtoMon_MAC_timedRecv;
    }
    if (c.monitoredLevels & PROTOMON_LEVEL_ROUTING)
    {
        if (!Original_Routing_sendMsg || !Original_Routing_recvMsg || !Original_Routing_timedRecvMsg || !Original_MAC_sendMsg || !Original_MAC_recvMsg || !Original_MAC_timedRecvMsg)
        {
            logMessage(ERROR, "Functions of routing & MAC layers must be registered.\n");
            fflush(stdout);
            exit(EXIT_FAILURE);
        }
        logMessage(INFO, "Monitoring enabled for Routing layer\n");
        fflush(stdout);
    }

    if (c.monitoredLevels & PROTOMON_LEVEL_MAC)
    {
        if (!Original_MAC_sendMsg || !Original_MAC_recvMsg || !Original_MAC_timedRecvMsg)
        {
            logMessage(ERROR, "Functions of MAC layer must be registered.\n");
            fflush(stdout);
            exit(EXIT_FAILURE);
        }
        logMessage(INFO, "Monitoring enabled for MAC layer\n");
        fflush(stdout);
    }

    // Set default values for config
    setConfigDefaults(&c);
    config = c;
    startTime = lastVizTime = lastMacWrite = lastNeighborWrite = lastRoutingWrite = time(NULL);
    initMetrics();

    initRoutingMetricsV2();

    // Enable visualization only when monitoring is enabled
    if (c.monitoredLevels != PROTOMON_LEVEL_NONE)
    {
        if (config.self != ADDR_SINK)
        {

            pthread_t sendMetricsT;
            if (pthread_create(&sendMetricsT, NULL, sendMetrics_func, NULL) != 0)
            {
                logMessage(ERROR, "Failed to create sendMetrics thread\n");
                exit(EXIT_FAILURE);
            }
        }
        else
        {
            installDependencies();
            initOutputFiles();
            // initOutputFilesV2();
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
    return (config.monitoredLevels & PROTOMON_LEVEL_ROUTING) ? ROUTING_OVERHEAD_SIZE : 0;
}

static uint16_t getMACOverhead()
{
    return (config.monitoredLevels & PROTOMON_LEVEL_MAC) ? MAC_OVERHEAD_SIZE : 0;
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
    uint8_t extData[MAX_PAYLOAD_SIZE];
    int extLen = len + overhead + 1; // null terminator
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

    // Set data
    memcpy(temp, data, len);
    temp += len;

    // Terminate the data field
    *temp = '\0';
    temp++;

    uint8_t path[5];
    uint8_t pathLen = sprintf(path, "%02d", config.self);
    memcpy(temp, path, pathLen);
    temp += pathLen;
    extLen += pathLen;

    if (config.loglevel >= TRACE)
    {
        logMessage(TRACE, "%s: ", __func__);
        for (int i = 0; i < overhead; i++)
            printf("%02X ", extData[i]);
        printf("|");
        for (int i = overhead; i < extLen; i++)
            printf(" %02X", extData[i]);
        printf("\n");
    }

    int ret = Original_Routing_sendMsg(dest, extData, extLen);

    // Capture metrics

    // TODO: Set condition based on config.serializedMetrics
    if (1)
    {
        uint16_t increment = 1;
        Metric_updateParamVal(&routingValues[dest], SENT, (void *)&increment);
    }
    else
    {

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
    }

    return ret;
}

int ProtoMon_Routing_recvMsg(Routing_Header *header, uint8_t *data)
{
    time_t start = time(NULL);
    uint16_t overhead = getRoutingOverhead();
    uint8_t extendedData[MAX_PAYLOAD_SIZE];
    if (extendedData == NULL)
    {
        logMessage(ERROR, "%s Error allocating memory for extendedData buffer\n", __func__);
        fflush(stdout);
        exit(EXIT_FAILURE);
    }
    int len = Original_Routing_recvMsg(header, extendedData);
    if (len <= 0)
    {
        return len;
    }
    uint8_t *temp = extendedData;
    uint8_t ctrl = *temp;

    if (config.loglevel >= TRACE)
    {
        logMessage(TRACE, "%s: ", __func__);
        for (int i = 0; i < overhead; i++)
            printf("%02X ", extendedData[i]);
        printf("|");
        for (int i = overhead; i < len; i++)
            printf(" %02X", extendedData[i]);
        printf("\n");
    }

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

        strcpy(data, temp);
        uint16_t dataLen = strlen(data);
        // data[dataLen] = '\0';
        temp += dataLen + 1;

        uint16_t latency = (time(NULL) - ts);
        if (config.loglevel >= DEBUG)
        {
            logMessage(DEBUG, "ProtoMon : %s hops: %d delay: %d s\n", data, numHops, latency);
            logMessage(DEBUG, "Path: %s\n", lastPath);
        }

        // Capture metrics
        // TODO: Set condition based on config.serializedMetrics
        if (1)
        {
            uint16_t increment = 1;
            Metric_updateParamVal(&routingValues[src], RECV, (void *)&increment);
            Metric_setParamVal(&routingValues[src], NUMHOPS, (void *)&numHops);
            Metric_setParamVal(&routingValues[src], LATENCY, (void *)&latency);
            Metric_setParamVal(&routingValues[src], RECV, (void *)&routingMetrics.data[src].recv);
        }
        else
        {

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

            logMessage(INFO, "### ProtoMon : hops: %d\n", *(uint8_t *)routingValues[src].params[NUMHOPS].value);

            memset(routingMetrics.data[src].path, 0, sizeof(routingMetrics.data[src].path));
            strcpy(routingMetrics.data[src].path, lastPath);
            sem_post(&routingMetrics.mutex);
        }

        return len - overhead;
    }
    else if (ctrl == CTRL_MAC || ctrl == CTRL_ROU || ctrl == CTRL_TAB)
    {
        if (config.self == ADDR_SINK)
        {
            uint8_t src = header->src;
            const char *fileName = (ctrl == CTRL_MAC) ? macCSV : (ctrl == CTRL_TAB ? networkCSV : routingCSV);

            // TODO: Set condition based on config.serialize
            if (1)
            {
                logMessage(INFO, "Received %s data of Node %02d: %d B\n", (ctrl == CTRL_MAC) ? "MAC" : (ctrl == CTRL_TAB ? "Topology" : "Routing"), src, len);
                deserializeMetricsPkt(extendedData, len, src, ctrl);
            }
            else
            {
                temp += sizeof(ctrl);
                int writeLen = writeBufferToFile(fileName, temp);
                if (writeLen <= 0)
                {
                    logMessage(ERROR, "Error writing to %s file!\n", fileName);
                    fflush(stdout);
                    exit(EXIT_FAILURE);
                }
                else
                {
                    logMessage(INFO, "Received %s data of Node %02d: %d B\n", (ctrl == CTRL_MAC) ? "MAC" : (ctrl == CTRL_TAB ? "Topology" : "Routing"), header->src, writeLen);
                }
            }

            // Write corresponding sink metrics to file
            time_t *lastWrite = (ctrl == CTRL_MAC) ? &lastMacWrite : (ctrl == CTRL_TAB ? &lastNeighborWrite : &lastRoutingWrite);
            if (time(NULL) - *lastWrite > config.sendIntervalS)
            {
                uint16_t bufferSize = SINK_MAX_BUFFER;
                uint8_t buffer[bufferSize];
                if (buffer == NULL)
                {
                    logMessage(ERROR, "Error allocating memory for %s data buffer!\n", ctrl == CTRL_MAC ? "MAC" : (ctrl == CTRL_TAB ? "Topology" : "Routing"));
                    fflush(stdout);
                    exit(EXIT_FAILURE);
                }

                uint16_t bufLen;
                // TODO: Set condition based on config.serialize
                if (1)
                {
                    bufLen = getMetricsCSV2(buffer, bufferSize, ctrl);
                }
                else
                {
                    bufLen = getMetricsCSV(buffer, bufferSize, ctrl);
                }

                if (bufLen)
                {
                    if (writeBufferToFile(fileName, buffer) <= 0)
                    {
                        logMessage(ERROR, "Error writing to %s file!\n", fileName);
                        fflush(stdout);
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

int ProtoMon_Routing_timedRecvMsg(Routing_Header *header, uint8_t *data, unsigned int timeout)
{
    time_t start = time(NULL);
    uint16_t overhead = getRoutingOverhead();
    uint8_t extendedData[MAX_PAYLOAD_SIZE];
    if (extendedData == NULL)
    {
        logMessage(ERROR, "%s Error allocating memory for extendedData buffer\n", __func__);
        fflush(stdout);
        exit(EXIT_FAILURE);
    }
    int len = Original_Routing_timedRecvMsg(header, extendedData, timeout);
    if (len <= 0)
    {
        return len;
    }
    uint8_t *temp = extendedData;
    uint8_t ctrl = *temp;
    uint16_t extLen = len - overhead;

    if (config.loglevel >= TRACE)
    {
        logMessage(TRACE, "%s: ", __func__);
        for (int i = 0; i < overhead; i++)
            printf("%02X ", extendedData[i]);
        printf("|");
        for (int i = overhead; i < len; i++)
            printf(" %02X", extendedData[i]);
        printf("\n");
    }

    if (ctrl == CTRL_MSG)
    {
        uint8_t src = header->src;
        extendedData[len] = '\0';

        // Extract routing monitoring fields
        uint8_t numHops;
        time_t ts;
        temp += sizeof(ctrl);
        memcpy(&numHops, temp, sizeof(numHops));
        temp += sizeof(numHops);
        memcpy(&ts, temp, sizeof(ts));
        temp += sizeof(ts);

        strcpy(data, temp);
        uint16_t dataLen = strlen(data);
        temp += dataLen + 1;

        uint16_t latency = (time(NULL) - ts);
        if (config.loglevel >= DEBUG)
        {
            logMessage(DEBUG, "ProtoMon : %s hops: %d delay: %d s\n", data, numHops, latency);
            logMessage(DEBUG, "Path: %s\n", lastPath);
        }

        // Capture metrics
        // TODO: Set condition based on config.serializedMetrics
        if (1)
        {
            uint16_t increment = 1;
            Metric_updateParamVal(&routingValues[src], RECV, (void *)&increment);
            Metric_setParamVal(&routingValues[src], NUMHOPS, (void *)&numHops);
            Metric_setParamVal(&routingValues[src], LATENCY, (void *)&latency);
            Metric_setParamVal(&routingValues[src], RECV, (void *)&routingMetrics.data[src].recv);
        }
        else
        {

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

            logMessage(INFO, "### ProtoMon : hops: %d\n", *(uint8_t *)routingValues[src].params[NUMHOPS].value);

            memset(routingMetrics.data[src].path, 0, sizeof(routingMetrics.data[src].path));
            strcpy(routingMetrics.data[src].path, lastPath);
            sem_post(&routingMetrics.mutex);
        }

        return extLen;
    }
    else if (ctrl == CTRL_MAC || ctrl == CTRL_ROU || ctrl == CTRL_TAB)
    {
        if (config.self == ADDR_SINK)
        {
            uint8_t src = header->src;
            const char *fileName = (ctrl == CTRL_MAC) ? macCSV : (ctrl == CTRL_TAB ? networkCSV : routingCSV);

            // TODO: Set condition based on config.serialize
            if (1)
            {
                logMessage(INFO, "Received %s data of Node %02d: %d B\n", (ctrl == CTRL_MAC) ? "MAC" : (ctrl == CTRL_TAB ? "Topology" : "Routing"), src, len);
                deserializeMetricsPkt(extendedData, len, src, ctrl);
            }
            else
            {
                temp += sizeof(ctrl);
                int writeLen = writeBufferToFile(fileName, temp);
                if (writeLen <= 0)
                {
                    logMessage(ERROR, "Error writing to %s file!\n", fileName);
                    fflush(stdout);
                    exit(EXIT_FAILURE);
                }
                else
                {
                    logMessage(INFO, "Received %s data of Node %02d: %d B\n", (ctrl == CTRL_MAC) ? "MAC" : (ctrl == CTRL_TAB ? "Topology" : "Routing"), header->src, writeLen);
                }
            }

            // Write corresponding sink metrics to file
            time_t *lastWrite = (ctrl == CTRL_MAC) ? &lastMacWrite : (ctrl == CTRL_TAB ? &lastNeighborWrite : &lastRoutingWrite);
            if (time(NULL) - *lastWrite > config.sendIntervalS)
            {
                uint16_t bufferSize = SINK_MAX_BUFFER;
                uint8_t buffer[bufferSize];
                if (buffer == NULL)
                {
                    logMessage(ERROR, "Error allocating memory for %s data buffer!\n", ctrl == CTRL_MAC ? "MAC" : (ctrl == CTRL_TAB ? "Topology" : "Routing"));
                    fflush(stdout);
                    exit(EXIT_FAILURE);
                }

                uint16_t bufLen;
                // TODO: Set condition based on config.serialize
                if (1)
                {
                    bufLen = getMetricsCSV2(buffer, bufferSize, ctrl);
                }
                else
                {
                    bufLen = getMetricsCSV(buffer, bufferSize, ctrl);
                }

                if (bufLen)
                {
                    if (writeBufferToFile(fileName, buffer) <= 0)
                    {
                        logMessage(ERROR, "Error writing to %s file!\n", fileName);
                        fflush(stdout);
                        exit(EXIT_FAILURE);
                    }
                    *lastWrite = time(NULL);
                }
            }
        }
    }
    uint16_t delay = lastVizTime == 0 ? config.initialSendWaitS : 0;
    if (config.self == ADDR_SINK && time(NULL) - lastVizTime > (delay + config.vizIntervalS))
    {
        generateGraph();
        lastVizTime = time(NULL);
    }

    return 0;
}

int ProtoMon_Routing_timedRecvMsgV2(Routing_Header *header, uint8_t *data, unsigned int timeout)
{
    time_t start = time(NULL);
    uint16_t overhead = getRoutingOverhead();
    uint8_t extendedData[MAX_PAYLOAD_SIZE];
    if (extendedData == NULL)
    {
        logMessage(ERROR, "%s Error allocating memory for extendedData buffer\n", __func__);
        fflush(stdout);
        exit(EXIT_FAILURE);
    }
    int len = Original_Routing_timedRecvMsg(header, extendedData, timeout);
    if (len <= 0)
    {
        return len;
    }
    uint8_t *temp = extendedData;
    uint8_t ctrl = *temp;
    uint16_t extLen = len - overhead;

    if (config.loglevel >= TRACE)
    {
        logMessage(TRACE, "%s: ", __func__);
        for (int i = 0; i < overhead; i++)
            printf("%02X ", extendedData[i]);
        printf("|");
        for (int i = overhead; i < len; i++)
            printf(" %02X", extendedData[i]);
        printf("\n");
    }

    if (ctrl == CTRL_MSG)
    {
        uint8_t src = header->src;
        extendedData[len] = '\0';

        // Extract routing monitoring fields
        uint8_t numHops;
        time_t ts;
        temp += sizeof(ctrl);
        memcpy(&numHops, temp, sizeof(numHops));
        temp += sizeof(numHops);
        memcpy(&ts, temp, sizeof(ts));
        temp += sizeof(ts);

        strcpy(data, temp);
        uint16_t dataLen = strlen(data);
        temp += dataLen + 1;

        uint16_t latency = (time(NULL) - ts);
        if (config.loglevel >= DEBUG)
        {
            logMessage(DEBUG, "ProtoMon : %s hops: %d delay: %d s\n", data, numHops, latency);
            logMessage(DEBUG, "Path: %s\n", lastPath);
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
        memset(routingMetrics.data[src].path, 0, sizeof(routingMetrics.data[src].path));
        strcpy(routingMetrics.data[src].path, lastPath);
        sem_post(&routingMetrics.mutex);

        return extLen;
    }
    else if (ctrl == CTRL_MET)
    {
        if (config.self == ADDR_SINK)
        {
            uint8_t metricsCount;
            memcpy(&metricsCount, ++temp, sizeof(metricsCount));
            temp += sizeof(metricsCount);

            logMessage(INFO, "Received %d metrics from Node %02d\n", metricsCount, header->src);

            for (uint8_t i = 0; i < metricsCount; i++)
            {
                uint8_t metricCtrl = *(temp++);
                uint16_t bufferLen;
                memcpy(&bufferLen, temp, sizeof(bufferLen));
                temp += sizeof(bufferLen);

                uint8_t *metricBuffer = (uint8_t *)malloc(bufferLen + 1);
                if (!metricBuffer)
                {
                    logMessage(ERROR, "%s: malloc failed\n", __func__);
                    fflush(stdout);
                    exit(EXIT_FAILURE);
                }
                memcpy(metricBuffer, temp, bufferLen);
                temp += bufferLen;
                metricBuffer[bufferLen] = '\0';

                const char *fileName = (metricCtrl == CTRL_MAC) ? macCSV : (metricCtrl == CTRL_TAB ? networkCSV : routingCSV);
                int writeLen = writeBufferToFile(fileName, metricBuffer);
                if (writeLen <= 0)
                {
                    logMessage(ERROR, "Error writing to %s file!\n", fileName);
                    fflush(stdout);
                    exit(EXIT_FAILURE);
                }
                free(metricBuffer);

                // Write corresponding sink metrics to file
                time_t *lastWrite = (metricCtrl == CTRL_MAC) ? &lastMacWrite : (metricCtrl == CTRL_TAB ? &lastNeighborWrite : &lastRoutingWrite);
                if (time(NULL) - *lastWrite > config.sendIntervalS)
                {
                    uint16_t bufferSize = MAX_PAYLOAD_SIZE;
                    uint8_t buffer[bufferSize];
                    if (buffer == NULL)
                    {
                        logMessage(ERROR, "Error allocating memory for %s data buffer!\n", metricCtrl == CTRL_MAC ? "MAC" : (metricCtrl == CTRL_TAB ? "Topology" : "Routing"));
                        fflush(stdout);
                        exit(EXIT_FAILURE);
                    }
                    uint16_t bufLen = getMetricsCSV(buffer, bufferSize, metricCtrl);
                    if (bufLen)
                    {
                        if (writeBufferToFile(fileName, buffer) <= 0)
                        {
                            logMessage(ERROR, "Error writing to %s file!\n", fileName);
                            fflush(stdout);
                            exit(EXIT_FAILURE);
                        }
                        *lastWrite = time(NULL);
                    }
                }
            }
        }
    }
    else
    {
        logMessage(ERROR, "ProtoMon: ctrl : %02X unknown\n", ctrl);
    }
    uint16_t delay = lastVizTime == 0 ? config.initialSendWaitS : 0;
    if (config.self == ADDR_SINK && time(NULL) - lastVizTime > (delay + config.vizIntervalS))
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

    uint8_t extData[MAX_PAYLOAD_SIZE];
    uint8_t *temp = extData;
    // if (dest != ADDR_BROADCAST) // exclude broadcast messages - beacons
    {
        // Check if msg packet
        uint8_t ctrl;
        uint8_t isMsg;
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

    if (config.loglevel >= TRACE)
    {
        logMessage(TRACE, "%s: ", __func__);
        for (int i = 0; i < overhead; i++)
            printf("%02X ", extData[i]);
        printf("|");
        for (int i = overhead; i < overhead + len; i++)
            printf(" %02X", extData[i]);
        printf("\n");
    }

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
    uint16_t extLen = len - overhead;
    uint8_t dest = h->recvH.dst_addr;

    if (config.loglevel >= TRACE)
    {
        logMessage(TRACE, "%s-IN: ", __func__);
        for (int i = 0; i < overhead; i++)
            printf("%02X ", extendedData[i]);
        printf("|");
        for (int i = overhead; i < len; i++)
            printf(" %02X", extendedData[i]);
        printf("\n");
    }

    // if (dest != ADDR_BROADCAST) // exclude broadcasts - beacons
    {
        // Check if msg packet
        uint8_t ctrl;
        uint8_t isMsg;
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
            uint16_t latency = (time(NULL) - mac_ts);
            if (config.loglevel >= DEBUG)
            {
                logMessage(DEBUG, "ProtoMon : hop src:%02d latency:%ds\n", src, latency);
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

            // Append self to path
            uint8_t path[5];
            uint8_t pathLen = sprintf(path, "%c%02d", pathSeparator, config.self);
            strcpy(extendedData + len, path);
            p = extendedData + len + pathLen;
            uint8_t totalPathLen = ((numHops + 1) * 3) - 1;
            p -= totalPathLen;
            if (config.loglevel >= DEBUG)
            {
                logMessage(DEBUG, "Path:%s\n", p);
            }
            memset(lastPath, 0, sizeof(lastPath));
            memcpy(lastPath, p, totalPathLen);
            extLen += pathLen;
        }
    }

    memcpy(data, temp, extLen);

    if (config.loglevel >= TRACE)
    {
        logMessage(TRACE, "%s-OUT: ", __func__);
        for (int i = 0; i < overhead; i++)
            printf("%02X ", extendedData[i]);
        printf("|");
        for (int i = overhead; i < extLen + overhead; i++)
            printf(" %02X", extendedData[i]);
        printf("\n");
    }

    return extLen;
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
    uint16_t extLen = len - overhead;
    uint8_t dest = h->recvH.dst_addr;
    if (config.loglevel >= TRACE)
    {
        logMessage(TRACE, "%s-IN: ", __func__);
        for (int i = 0; i < overhead; i++)
            printf("%02X ", extendedData[i]);
        printf("|");
        for (int i = overhead; i < len; i++)
            printf(" %02X", extendedData[i]);
        printf("\n");
    }

    // if (dest != ADDR_BROADCAST) // exclude broadcasts - beacons
    {
        // Check if msg packet
        uint8_t ctrl;
        uint8_t isMsg;
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
            uint16_t latency = (time(NULL) - mac_ts);
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

            // Append self to path
            uint8_t path[5];
            uint8_t pathLen = sprintf(path, "%c%02d", pathSeparator, config.self);
            strcpy(extendedData + len, path);
            p = extendedData + len + pathLen;
            uint8_t totalPathLen = ((numHops + 1) * 3) - 1;
            p -= totalPathLen;
            if (config.loglevel >= DEBUG)
            {
                logMessage(DEBUG, "Path:%s\n", p);
            }
            memset(lastPath, 0, sizeof(lastPath));
            memcpy(lastPath, p, totalPathLen);
            extLen += pathLen;
        }
    }

    memcpy(data, temp, extLen);

    if (config.loglevel >= TRACE)
    {
        logMessage(TRACE, "%s-OUT: ", __func__);
        for (int i = 0; i < overhead; i++)
            printf("%02X ", extendedData[i]);
        printf("|");
        for (int i = overhead; i < extLen + overhead; i++)
            printf(" %02X", extendedData[i]);
        printf("\n");
    }

    return extLen;
}

static int writeBufferToFile(const uint8_t *fileName, uint8_t *temp)
{
    if (config.loglevel >= DEBUG)
    {
        logMessage(DEBUG, "%s: %ld\n%s\n", fileName, strlen(temp), temp);
    }
    FILE *file = fopen(fileName, "a");
    if (file == NULL)
    {
        logMessage(ERROR, "Error opening %s file!\n", fileName);
        fflush(stdout);
        exit(EXIT_FAILURE);
    }
    int len = fprintf(file, "%s", temp);
    fflush(file);
    fclose(file);
    return len;
}
