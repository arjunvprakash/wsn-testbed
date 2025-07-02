#include <errno.h>     // errno
#include <pthread.h>   // pthread_create
#include <semaphore.h> // sem_init, sem_wait, sem_trywait, sem_timedwait
#include <stdbool.h>   // bool, true, false
#include <stdio.h>     // printf
#include <stdlib.h>    // rand, malloc, free, exit
#include <string.h>    // memcpy, strerror, strrok
#include <time.h>      // time
#include <unistd.h>    // sleep, exec, chdir

#include "SMRP.h"
#include "../util.h"

#define PACKETQ_SIZE 16

// Packet control flags
#define CTRL_PKT '\x45' // SMRP data packet
#define CTRL_BCN '\x47' // SMRP beacon

typedef struct Beacon
{
    uint8_t ctrl;
} Beacon;

typedef struct DataPacket
{
    uint8_t ctrl;
    uint8_t dest;
    uint8_t src;
    uint16_t len;
    uint8_t *data;
} DataPacket;

typedef struct PacketQueue
{
    unsigned int begin, end;
    struct DataPacket packet[PACKETQ_SIZE];
    sem_t mutex, full, free;
} PacketQueue;

typedef struct
{
    uint8_t addr;
    int RSSI;
    unsigned short hopsToSink;
    time_t lastSeen;
    Routing_LinkType link;
    Routing_NodeState state;
} NodeInfo;

typedef struct
{
    NodeInfo nodes[MAX_ACTIVE_NODES];
    sem_t mutex;
    uint8_t numActive;
    uint8_t numNodes;
    time_t lastCleanupTime;
    uint8_t minAddr, maxAddr;
} ActiveNodes;

typedef struct SMRP_Params
{
    uint16_t beaconsTx;
    uint16_t beaconsRx;
} SMRP_Params;

typedef struct Metrics
{
    // mutex and data for each node
    sem_t mutex;
    SMRP_Params data[MAX_ACTIVE_NODES];
} Metrics;

static Metrics metrics;

static PacketQueue sendQ, recvQ;
static pthread_t recvT;
static pthread_t sendT;
static MAC mac;
static const unsigned short headerSize = 5; // [ ctrl | dest | src | len[2] ]
static ActiveNodes neighbours;
static SMRP_Config config;

int (*Routing_sendMsg)(uint8_t dest, uint8_t *data, unsigned int len) = SMRP_sendMsg;
int (*Routing_recvMsg)(Routing_Header *h, uint8_t *data) = SMRP_recvMsg;
int (*Routing_timedRecvMsg)(Routing_Header *h, uint8_t *data, unsigned int timeout) = SMRP_timedRecvMsg;

static void sendQ_init();
static void recvQ_init();
static void sendQ_enqueue(DataPacket msg);
static DataPacket sendQ_dequeue();
static void recvQ_enqueue(DataPacket msg);
static DataPacket recvMsgQ_dequeue();
static void *recvPackets_func(void *args);
static DataPacket deserializePacket(uint8_t *pkt);
static void *sendPackets_func(void *args);
static uint8_t recvQ_timed_dequeue(DataPacket *msg, struct timespec *ts);
static void *receiveBeaconHandler(void *args);
static void senseNeighbours();
static void updateActiveNodes(uint8_t addr, int RSSI);
static void changeParent();
static void initNeighbours();
static void cleanupInactiveNodes();
static void sendBeacon();
static void *sendBeaconPeriodic(void *args);
char *getNodeStateStr(const Routing_NodeState state);
char *getNodeRoleStr(const Routing_LinkType link);

static void initMetrics();
static void setConfigDefaults(SMRP_Config *config);

uint8_t Routing_getnextHop(uint8_t src, uint8_t prev, uint8_t dest, uint8_t maxTries)
{
    ActiveNodes activenodes = neighbours;
    int i = 0;
    do
    {
        NodeInfo node = activenodes.nodes[(uint8_t)randInRange(activenodes.minAddr, activenodes.maxAddr)];
        if (node.state == ACTIVE && node.addr != src && node.addr != prev)
        {
            return node.addr;
        }
    } while (i < maxTries);

    return dest == 0 ? ADDR_SINK : dest;
}

// Initialize the SMRP
int SMRP_init(SMRP_Config c)
{
    // Make init idempotent
    if (config.self != 0)
    {
        return 1;
    }

    pthread_t sendBeaconT;
    setConfigDefaults(&c);
    config = c;
    srand(config.self * time(NULL));
    mac.debug = 0;
    if (config.loglevel > INFO)
    {
        mac.debug = 1;
    }
    mac.recvTimeout = config.recvTimeoutMs;
    mac.maxtrials = 2;
    ALOHA_init(&mac, config.self);
    sendQ_init();
    recvQ_init();
    initNeighbours();
    initMetrics();

    if (pthread_create(&recvT, NULL, recvPackets_func, &mac) != 0)
    {
        logMessage(ERROR, "Failed to create Routing receive thread\n");
        fflush(stdout);
        exit(EXIT_FAILURE);
    }

    senseNeighbours();

    if (config.self != ADDR_SINK)
    {
        if (pthread_create(&sendT, NULL, sendPackets_func, &mac) != 0)
        {
            logMessage(ERROR, "Failed to create Routing send thread\n");
            fflush(stdout);
            exit(EXIT_FAILURE);
        }
    }
    // Beacon thread
    if (pthread_create(&sendBeaconT, NULL, sendBeaconPeriodic, &mac) != 0)
    {
        logMessage(ERROR, "Failed to create sendBeaconPeriodic thread");
        fflush(stdout);
        exit(EXIT_FAILURE);
    }
    return 1;
}

// Send a message via SMRP
int SMRP_sendMsg(uint8_t dest, uint8_t *data, unsigned int len)
{
    DataPacket msg;
    msg.ctrl = CTRL_PKT;
    msg.dest = dest;
    msg.src = config.self;
    msg.len = len;
    msg.data = (uint8_t *)malloc(len);
    if (msg.data)
    {
        memcpy(msg.data, data, len);
    }
    else
    {
        logMessage(ERROR, "msg.data is NULL %s:%d\n", __FILE__, __LINE__);
        fflush(stdout);
        return 0;
    }
    sendQ_enqueue(msg);
    return 1;
}

// Receive a message via SMRP
int SMRP_recvMsg(Routing_Header *header, uint8_t *data)
{
    DataPacket msg = recvMsgQ_dequeue();
    header->dst = msg.dest;
    header->RSSI = mac.RSSI;
    header->src = msg.src;
    header->prev = mac.recvH.src_addr;
    if (msg.data)
    {
        memcpy(data, msg.data, msg.len);
        free(msg.data);
    }
    else
    {
        logMessage(ERROR, "msg.data is NULL %s:%d\n", __FILE__, __LINE__);
        fflush(stdout);
        return 0;
    }

    return msg.len;
}

// Receive a message via SMRP. Blocks the thread till the timeout.
// Returns 0 for timeout, -1 for error
int SMRP_timedRecvMsg(Routing_Header *header, uint8_t *data, unsigned int timeout)
{

    DataPacket msg;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout;

    int result = recvQ_timed_dequeue(&msg, &ts);
    if (result <= 0)
    {
        return result;
    }

    // Populate header with message details
    header->dst = msg.dest;
    header->RSSI = mac.RSSI;
    header->src = msg.src;
    header->prev = mac.recvH.src_addr;

    if (msg.data != NULL)
    {
        memcpy(data, msg.data, msg.len);
        free(msg.data);
    }
    else
    {
        logMessage(ERROR, "msg.data is NULL %s:%d\n", __FILE__, __LINE__);
        fflush(stdout);
        return 0;
    }

    return msg.len;
}

static uint8_t recvQ_timed_dequeue(DataPacket *msg, struct timespec *ts)
{
    if (sem_timedwait(&recvQ.full, ts) == -1)
    {
        if (errno == ETIMEDOUT)
        {
            return 0;
        }
        return -1;
    }

    sem_wait(&recvQ.mutex);
    *msg = recvQ.packet[recvQ.begin];
    recvQ.begin = (recvQ.begin + 1) % PACKETQ_SIZE;
    sem_post(&recvQ.mutex);
    sem_post(&recvQ.free);
    return 1;
}

static bool sendQ_tryenqueue(DataPacket msg)
{
    if (sem_trywait(&sendQ.free) == -1)
        return false;

    sem_wait(&sendQ.mutex);
    sendQ.packet[sendQ.end] = msg;
    sendQ.end = (sendQ.end + 1) % PACKETQ_SIZE;
    sem_post(&sendQ.mutex);
    sem_post(&sendQ.full);

    return true;
}

static bool recvQ_tryenqueue(DataPacket msg)
{
    if (sem_trywait(&recvQ.free) == -1)
        return false;

    sem_wait(&recvQ.mutex);
    recvQ.packet[recvQ.end] = msg;
    recvQ.end = (recvQ.end + 1) % PACKETQ_SIZE;
    sem_post(&recvQ.mutex);
    sem_post(&recvQ.full);

    return true;
}

static bool sendQ_trydequeue(DataPacket *msg)
{
    if (sem_trywait(&sendQ.full) == -1)
        return false;

    sem_wait(&sendQ.mutex);
    *msg = sendQ.packet[sendQ.begin];
    sendQ.begin = (sendQ.begin + 1) % PACKETQ_SIZE;
    sem_post(&sendQ.mutex);
    sem_post(&sendQ.free);

    return true;
}

static bool recvQ_trydequeue(DataPacket *msg)
{
    if (sem_trywait(&recvQ.full) == -1)
        return false;

    sem_wait(&recvQ.mutex);
    *msg = recvQ.packet[recvQ.begin];
    recvQ.begin = (recvQ.begin + 1) % PACKETQ_SIZE;
    sem_post(&recvQ.mutex);
    sem_post(&recvQ.free);

    return true;
}

static void sendQ_init()
{
    sendQ.begin = 0;
    sendQ.end = 0;
    sem_init(&sendQ.full, 0, 0);
    sem_init(&sendQ.free, 0, PACKETQ_SIZE);
    sem_init(&sendQ.mutex, 0, 1);
}

static void recvQ_init()
{
    recvQ.begin = 0;
    recvQ.end = 0;
    sem_init(&recvQ.full, 0, 0);
    sem_init(&recvQ.free, 0, PACKETQ_SIZE);
    sem_init(&recvQ.mutex, 0, 1);
}

static void sendQ_enqueue(DataPacket msg)
{
    time_t start = time(NULL);
    sem_wait(&sendQ.free);
    sem_wait(&sendQ.mutex);
    sendQ.packet[sendQ.end] = msg;
    sendQ.end = (sendQ.end + 1) % PACKETQ_SIZE;
    sem_post(&sendQ.mutex);
    sem_post(&sendQ.full);
}

static DataPacket sendQ_dequeue()
{
    sem_wait(&sendQ.full);
    sem_wait(&sendQ.mutex);
    DataPacket msg = sendQ.packet[sendQ.begin];
    sendQ.begin = (sendQ.begin + 1) % PACKETQ_SIZE;
    sem_post(&sendQ.mutex);
    sem_post(&sendQ.free);
    return msg;
}

static void recvQ_enqueue(DataPacket msg)
{
    sem_wait(&recvQ.free);
    sem_wait(&recvQ.mutex);
    recvQ.packet[recvQ.end] = msg;
    recvQ.end = (recvQ.end + 1) % PACKETQ_SIZE;
    sem_post(&recvQ.mutex);
    sem_post(&recvQ.full);
}

static DataPacket recvMsgQ_dequeue()
{
    sem_wait(&recvQ.full);
    sem_wait(&recvQ.mutex);
    DataPacket msg = recvQ.packet[recvQ.begin];
    recvQ.begin = (recvQ.begin + 1) % PACKETQ_SIZE;
    sem_post(&recvQ.mutex);
    sem_post(&recvQ.free);
    return msg;
}

static void *recvPackets_func(void *args)
{
    unsigned int total[MAX_ACTIVE_NODES] = {0};
    MAC *macTemp = (MAC *)args;
    time_t start = time(NULL);
    time_t current;
    while (1)
    {
        uint8_t *pkt = (uint8_t *)malloc(240);
        if (!pkt)
        {
            continue;
        }
        // int pktSize = ALOHA_recv(macTemp, pkt);
        int pktSize = MAC_timedRecv(macTemp, pkt, 1);
        if (pktSize == 0)
        {
            free(pkt);
            continue;
        }
        uint8_t ctrl = *pkt;
        if (ctrl == CTRL_PKT)
        {
            uint8_t dest = *(pkt + sizeof(ctrl));
            uint8_t src = *(pkt + sizeof(ctrl) + sizeof(dest));
            updateActiveNodes(mac.recvH.src_addr, mac.RSSI);
            if (dest == config.self)
            {
                DataPacket msg = deserializePacket(pkt);
                // Keep
                if (msg.len > 0)
                {
                    recvQ_enqueue(msg);
                }
            }
            else // Forward
            {
                uint8_t nextHop = Routing_getnextHop(src, mac.recvH.src_addr, dest, config.maxTries);
                if (rand() % 100 < 0)
                {
                    nextHop = dest;
                }
                if (MAC_send(macTemp, nextHop, pkt, pktSize))
                {
                    logMessage(INFO, "FWD: %02d -> %02d total: %02d\n", src, nextHop, ++total[src]);
                }
                else
                {
                    logMessage(ERROR, "%s - Error FWD: %02d -> %02d\n", timestamp(), src, nextHop);
                }
            }
        }
        else if (ctrl == CTRL_BCN)
        {
            Beacon *beacon = (Beacon *)pkt;
            if (config.loglevel >= DEBUG)
            {
                logMessage(DEBUG, "%s -Beacon src: %02d (%d)\n", timestamp(), mac.recvH.src_addr, mac.RSSI);
            }
            updateActiveNodes(mac.recvH.src_addr, mac.RSSI);
            metrics.data[mac.recvH.src_addr].beaconsRx++;
        }
        else
        {
            if (config.loglevel >= DEBUG)
            {
                logMessage(DEBUG, "%s - SMRP : Unknown control flag %02d \n", timestamp(), ctrl);
            }
        }
        free(pkt);
        usleep((rand() % 100000) + 700000); // Sleep 100ms + 1s to avoid busy waiting
    }
    return NULL;
}

// Construct RoutingMessage
static DataPacket deserializePacket(uint8_t *pkt)
{
    DataPacket msg;
    msg.ctrl = *pkt;
    pkt += sizeof(msg.ctrl);

    msg.dest = *pkt;
    pkt += sizeof(msg.dest);

    msg.src = *pkt;
    pkt += sizeof(msg.src);

    memcpy(&msg.len, pkt, sizeof(msg.len));
    pkt += sizeof(msg.len);

    if (msg.len > 0)
    {
        msg.data = (uint8_t *)malloc(msg.len);
        memcpy(msg.data, pkt, msg.len);
    }
    else
    {
        msg.data = NULL;
    }
    return msg;
}

static int serializePacketV2(DataPacket msg, uint8_t *routePkt)
{
    uint16_t routePktSize = msg.len + headerSize;
    if (routePkt == NULL)
    {
        return -1;
    }

    uint8_t *p = routePkt;
    *p = msg.ctrl;
    p += sizeof(msg.ctrl);

    // Set dest
    *p = msg.dest;
    p += sizeof(msg.dest);

    // Set source as self
    *p = config.self;
    p += sizeof(config.self);

    // Set actual msg length
    memcpy(p, &msg.len, sizeof(msg.len));
    p += sizeof(msg.len);

    // Set msg
    memcpy(p, msg.data, msg.len);
    free(msg.data);

    return routePktSize;
}

static void *sendPackets_func(void *args)
{
    MAC *macTemp = (MAC *)args;
    while (1)
    {
        DataPacket msg = sendQ_dequeue();
        uint8_t *pkt = (uint8_t *)malloc(msg.len + headerSize);
        if (!pkt)
        {
            continue;
        }
        unsigned int pktSize = serializePacketV2(msg, pkt);
        if (pktSize < 0)
        {
            continue;
        }
        time_t start = time(NULL);
        uint8_t nextHop = Routing_getnextHop(msg.src, -1, msg.dest, config.maxTries);
        // nextHop = msg.dest;
        if (!MAC_send(macTemp, nextHop, pkt, pktSize))
        {
            logMessage(ERROR, "%s - MAC_send failed %s:%d\n", timestamp(), __FILE__, __LINE__);
        }
        else
        {
        }
        free(pkt);
        usleep(1000000); // Sleep 1s
    }
    return NULL;
}

static void senseNeighbours()
{
    do
    {
        uint16_t count = 0;
        if (config.loglevel >= DEBUG)
        {
            logMessage(DEBUG, "Sending beacons...\n");
        }

        time_t start = time(NULL);
        do
        {
            sendBeacon();
            count++;
            usleep(randInRange(800000, 1200000));
        } while (time(NULL) - start < config.senseDurationS);

        if (config.loglevel >= DEBUG)
        {
            logMessage(DEBUG, "Sent %d beacons...\n", count);
        }

        if (neighbours.numActive == 0)
        {
            logMessage(INFO, "No neighbors detected. Trying again...\n");
        }
        else
        {
            break;
        }

    } while (1);

    logMessage(INFO, "Active neighbours: %d \n", neighbours.numActive);
    fflush(stdout);
}

static void updateActiveNodes(uint8_t addr, int RSSI)
{
    sem_wait(&neighbours.mutex);
    NodeInfo *nodePtr = &neighbours.nodes[addr];
    uint8_t numActive;
    bool new = nodePtr->state == UNKNOWN;
    bool child = false;
    if (new)
    {
        nodePtr->addr = addr;
        nodePtr->state = ACTIVE;
        neighbours.numActive++;
        neighbours.numNodes++;
        numActive = neighbours.numActive;
        if (addr > neighbours.maxAddr)
        {
            neighbours.maxAddr = addr;
        }
        if (addr < neighbours.minAddr)
        {
            neighbours.minAddr = addr;
        }
    }
    else
    {
        if (nodePtr->state == INACTIVE)
        {
            nodePtr->state = ACTIVE;
            neighbours.numActive++;
        }
    }
    // Random assignment of link
    if (new)
    {
        if (addr == ADDR_SINK)
        {
            nodePtr->link = OUTBOUND;
        }
        else
        {
            nodePtr->link = OUTBOUND;
            // nodePtr->link = rand() % 100 < 50 ? OUTBOUND : ROLE_NODE;
        }
    }    
    nodePtr->RSSI = RSSI;
    nodePtr->lastSeen = time(NULL);
    sem_post(&neighbours.mutex);
    if (new)
    {
        // if (config.loglevel >= DEBUG)
        {
            logMessage(DEBUG, "New %s: %02d (%02d)\n", "neighbour", addr, RSSI);
            logMessage(DEBUG, "Active neighbours: %d \n", neighbours.numActive);
        }
    }
}

void initNeighbours()
{
    sem_init(&neighbours.mutex, 0, 1);
    neighbours.numActive = 0;
    neighbours.numNodes = 0;
    memset(neighbours.nodes, 0, sizeof(neighbours.nodes));
    for (uint8_t i = 0; i < MAX_ACTIVE_NODES; i++)
    {
        neighbours.nodes[i].state = UNKNOWN;
    }
    neighbours.minAddr = MAX_ACTIVE_NODES - 1;
    neighbours.maxAddr = 0;
    neighbours.lastCleanupTime = time(NULL);
}

static void cleanupInactiveNodes()
{
    time_t currentTime = time(NULL);
    bool parentInactive = false;
    sem_wait(&neighbours.mutex);
    uint8_t numActive = neighbours.numActive;
    for (uint8_t i = neighbours.minAddr, inactive = 0; i <= neighbours.maxAddr && inactive < numActive; i++)
    {
        NodeInfo *nodePtr = &neighbours.nodes[i];
        if (nodePtr->state == ACTIVE && ((currentTime - nodePtr->lastSeen) >= config.nodeTimeoutS))
        {
            nodePtr->state = INACTIVE;
            nodePtr->link = IDLE;
            neighbours.numActive--;
            inactive++;
            logMessage(INFO, "Node %02d inactive.\n", nodePtr->addr);
        }
    }
    numActive = neighbours.numActive;
    neighbours.lastCleanupTime = time(NULL);
    sem_post(&neighbours.mutex);

    if (config.loglevel >= DEBUG)
    {
        logMessage(DEBUG, "Active neighbours: %d \n", neighbours.numActive);
    }
}

static void sendBeacon()
{
    Beacon beacon;
    beacon.ctrl = CTRL_BCN;
    if (config.loglevel >= DEBUG)
    {
        logMessage(DEBUG, "Sending beacon\n");
    }
    if (!MAC_send(&mac, ADDR_BROADCAST, (uint8_t *)&beacon, sizeof(Beacon)))
    {
        logMessage(INFO, "MAC_send failed %s:%d\n", __FILE__, __LINE__);
    }
    else
    {
        metrics.data[0].beaconsTx++;
    }
}

static void *sendBeaconPeriodic(void *args)
{
    sleep(config.beaconIntervalS);
    while (1)
    {
        sendBeacon();
        if ((time(NULL) - neighbours.lastCleanupTime) > config.nodeTimeoutS)
        {
            cleanupInactiveNodes();
        }
        sleep(config.beaconIntervalS);
    }
    return NULL;
}

char *getNodeRoleStr(const Routing_LinkType link)
{
    char *roleStr;
    switch (link)
    {
    case OUTBOUND:
        roleStr = "PARENT";
        break;
    case IDLE:
        roleStr = "NODE";
        break;
    default:
        roleStr = "ERROR";
        break;
    }
    return roleStr;
}

char *getNodeStateStr(const Routing_NodeState state)
{
    char *stateStr;
    switch (state)
    {
    case UNKNOWN:
        stateStr = "UNKNOWN";
        break;
    case ACTIVE:
        stateStr = "ACTIVE";
        break;
    case INACTIVE:
        stateStr = "INACTIVE";
        break;
    default:
        stateStr = "ERROR";
        break;
    }
    return stateStr;
}

uint8_t Routing_getHeaderSize()
{
    return headerSize;
}

uint8_t Routing_isDataPkt(uint8_t ctrl)
{
    return ctrl == CTRL_PKT;
}

uint8_t *Routing_getMetricsHeader()
{
    return "AggBeaconsSent,TotalBeaconsRecv";
}

int Routing_getMetricsData(uint8_t *buffer, uint8_t addr)
{
    const SMRP_Params data = metrics.data[addr];
    int rowlen = sprintf(buffer, "%d,%d", metrics.data[0].beaconsTx, data.beaconsRx);
    sem_wait(&metrics.mutex);
    metrics.data[addr] = (SMRP_Params){0};
    metrics.data[0].beaconsTx = 0;
    sem_post(&metrics.mutex);
    return rowlen;
}

static void initMetrics()
{
    sem_init(&metrics.mutex, 0, 1);
    sem_wait(&metrics.mutex);
    memset(&metrics.data, 0, sizeof(metrics.data));
    sem_post(&metrics.mutex);
}

int Routing_getTopologyData(char *buffer, uint16_t size)
{
    const ActiveNodes activeNodes = neighbours;
    uint8_t max = neighbours.maxAddr;
    uint8_t min = neighbours.minAddr;
    int offset = 0;
    uint8_t src = config.self;
    time_t timestamp = time(NULL);
    for (uint8_t addr = min; addr <= max; addr++)
    {
        NodeInfo node = activeNodes.nodes[addr];
        if (node.state != UNKNOWN)
        {
            uint8_t row[100];
            int rowlen = sprintf(row, "%ld,%d,%d,%d,%d,%d\n", (long)timestamp, src, addr, node.state, node.link, node.RSSI);

            // Clear timestamp to avoid duplicate
            timestamp = 0L;

            // if (offset + rowlen < size)
            if (1)
            {
                memcpy(buffer + offset, row, rowlen);
                offset += rowlen;
            }
            else
            {
                break;
            }
        }
    }
    return offset;
}

void setConfigDefaults(SMRP_Config *config)
{
    if (config->beaconIntervalS == 0)
    {
        config->beaconIntervalS = 30;
    }
    if (config->senseDurationS == 0)
    {
        config->senseDurationS = 15;
    }
    if (config->nodeTimeoutS == 0)
    {
        config->nodeTimeoutS = 60;
    }
    // if (config->recvTimeoutMs == 0)
    // {
    //     config->recvTimeoutMs = 1000;
    // }
    if (config->loglevel == 0)
    {
        config->loglevel = INFO;
    }
    if (config->maxTries == 0)
    {
        config->maxTries = 2;
    }
}

uint8_t *Routing_getTopologyHeader()
{
    return "Timestamp,Source,Address,State,LinkType,RSSI";
}