#include <errno.h>     // errno
#include <pthread.h>   // pthread_create
#include <semaphore.h> // sem_init, sem_wait, sem_trywait, sem_timedwait
#include <stdbool.h>   // bool, true, false
#include <stdio.h>     // printf
#include <stdlib.h>    // rand, malloc, free, exit
#include <string.h>    // memcpy, strerror, strrok
#include <time.h>      // time
#include <unistd.h>    // sleep, exec, chdir

#include "STRP.h"
#include "../util.h"

#define PACKETQ_SIZE 32
#define MIN_RSSI -128
#define INITIAL_PARENT 0

// Packet control flags
#define CTRL_PKT '\x45' // STRP packet
#define CTRL_BCN '\x47' // STRP beacon

typedef struct Beacon
{
    uint8_t ctrl;
    uint8_t parent;
    int8_t parentRSSI;
} Beacon;

typedef struct DataPacket
{
    uint8_t ctrl;
    uint8_t dest;
    uint8_t src;
    uint16_t len;
    uint8_t *data;
    Routing_Header metadata;
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
    int8_t RSSI;
    time_t lastSeen;
    Routing_LinkType link;
    uint8_t parent;
    Routing_NodeState state;
    int8_t parentRSSI;
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


typedef struct STRP_Params
{
    uint16_t parentChanges;
    uint16_t beaconsSent;
    uint16_t beaconsRecv;
} STRP_Params;

typedef struct Metrics
{
    // mutex and data for each node
    sem_t mutex;
    STRP_Params data[MAX_ACTIVE_NODES];
} Metrics;

static Metrics metrics;

static PacketQueue sendQ, recvQ;
static uint16_t sendSeq[MAX_ACTIVE_NODES] = {0};
static uint16_t recvSeq[MAX_ACTIVE_NODES] = {0};
static pthread_t recvT;
static pthread_t sendT;

static const unsigned short headerSize = sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint16_t); // [ ctrl | dest | src | seqId[2] | len[2] ]
static uint8_t parentAddr;
static ActiveNodes neighbours;
static uint8_t loopyParent;
static STRP_Config config;

int (*Routing_sendMsg)(uint8_t dest, uint8_t *data, unsigned int len) = STRP_sendMsg;
int (*Routing_recvMsg)(Routing_Header *h, uint8_t *data) = STRP_recvMsg;
int (*Routing_timedRecvMsg)(Routing_Header *h, uint8_t *data, unsigned int timeout) = STRP_timedRecvMsg;

static void sendQ_init();
static void recvQ_init();
static void sendQ_enqueue(DataPacket msg);
static DataPacket sendQ_dequeue();
static void recvQ_enqueue(DataPacket msg);
static DataPacket recvMsgQ_dequeue();
static void *recvPackets_func(void *args);
static DataPacket deserializePacket(uint8_t *pkt);
static void *sendPackets_func(void *args);
static int serializePacket(DataPacket msg, uint8_t **routePkt);
static uint8_t recvQ_timed_dequeue(DataPacket *msg, struct timespec *ts);
static void senseNeighbours();
static void updateActiveNodes(uint8_t addr, int8_t RSSI, uint8_t parent, int8_t parentRSSI);
static void changeParent();
static void initNeighbours();
static void cleanupInactiveNodes();
static void selectRandomLowerNeighbour();
static void selectRandomNeighbour();
static void selectNextLowerNeighbour();
static void selectClosestNeighbour();
static void selectClosestLowerNeighbour();
static void sendBeacon();
static void *sendBeaconPeriodic(void *args);
char *getNodeStateStr(const Routing_NodeState state);
char *getNodeRoleStr(const Routing_LinkType link);
static char *getRoutingStrategyStr();

static void initMetrics();
static void setConfigDefaults(STRP_Config *config);

int STRP_init(STRP_Config c)
{
    // Make init idempotent
    if (config.self != 0)
    {
        return 1;
    }

    if (c.strategy == FIXED && c.self != ADDR_SINK && c.parentAddr == 0)
    {
        logMessage(ERROR, "STRP: parentAddr not provided with FIXED strategy.");
        exit(EXIT_FAILURE);
    }

    pthread_t sendBeaconT;
    setConfigDefaults(&c);
    config = c;

    ALOHA_init(config.mac, config.self);
    if (config.loglevel > INFO)
    {
        config.mac->debug = 1;
    }

    sendQ_init();
    recvQ_init();
    initNeighbours();
    initMetrics();

    if (pthread_create(&recvT, NULL, recvPackets_func, NULL) != 0)
    {
        logMessage(ERROR, "STRP: Failed to create Routing receive thread");
        exit(EXIT_FAILURE);
    }

    logMessage(INFO, "Routing Strategy:  %s\n", getRoutingStrategyStr());

    senseNeighbours();

    if (config.self != ADDR_SINK)
    {
        if (pthread_create(&sendT, NULL, sendPackets_func, NULL) != 0)
        {
            logMessage(ERROR, "STRP: Failed to create Routing send thread");
            exit(EXIT_FAILURE);
        }
    }
    // Beacon thread
    if (pthread_create(&sendBeaconT, NULL, sendBeaconPeriodic, NULL) != 0)
    {
        logMessage(ERROR, "STRP: Failed to create sendBeaconPeriodic thread");
        exit(EXIT_FAILURE);
    }
    return 1;
}

int STRP_sendMsg(uint8_t dest, uint8_t *data, unsigned int len)
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
        printf("# %s - Error: msg.data is NULL %s:%d\n", timestamp(), __FILE__, __LINE__);
        return 0;
    }
    sendQ_enqueue(msg);
    return 1;
}

int STRP_recvMsg(Routing_Header *header, uint8_t *data)
{
    DataPacket msg = recvMsgQ_dequeue();

    // Populate header with message metadata
    header->dst = msg.metadata.dst;
    header->RSSI = msg.metadata.RSSI;
    header->src = msg.src;
    header->prev = msg.metadata.prev;

    if (msg.data)
    {
        memcpy(data, msg.data, msg.len);
        free(msg.data);
    }
    else
    {
        printf("# %s - Error: msg.data is NULL %s:%d\n", timestamp(), __FILE__, __LINE__);
        return 0;
    }

    return msg.len;
}

int STRP_timedRecvMsg(Routing_Header *header, uint8_t *data, unsigned int timeout)
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

    // Populate header with message metadata
    header->dst = msg.metadata.dst;
    header->RSSI = msg.metadata.RSSI;
    header->src = msg.src;
    header->prev = msg.metadata.prev;

    if (msg.data != NULL)
    {
        memcpy(data, msg.data, msg.len);
        free(msg.data);
    }
    else
    {
        printf("# %s - Error: msg.data is NULL %s:%d\n", timestamp(), __FILE__, __LINE__);
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
    time_t start = time(NULL);
    time_t current;
    while (1)
    {
        uint8_t *pkt = (uint8_t *)malloc(MAX_PAYLOAD_SIZE);
        if (!pkt)
        {
            continue;
        }

        int pktSize = MAC_timedRecv(config.mac, pkt, 1);
        if (pktSize == 0)
        {
            free(pkt);
            continue;
        }
        Routing_Header metadata;
        metadata.prev = config.mac->recvH.src_addr;
        metadata.RSSI = config.mac->RSSI;

        uint8_t ctrl = *pkt;
        if (ctrl == CTRL_PKT)
        {
            uint8_t dest = *(pkt + sizeof(ctrl));
            uint8_t src = *(pkt + sizeof(ctrl) + sizeof(dest));
            updateActiveNodes(metadata.prev, metadata.RSSI, ADDR_BROADCAST, MIN_RSSI);

            if (config.loglevel >= TRACE)
            {
                logMessage(TRACE, "STRP:%s: ", __func__);
                for (int i = 0; i < headerSize; i++)
                    printf("%02X ", pkt[i]);
                printf("|");
                for (int i = headerSize; i < pktSize; i++)
                    printf(" %02X", pkt[i]);
                printf("\n");
            }

            if (dest == config.self)
            {
                DataPacket msg = deserializePacket(pkt);
                // Keep
                if (msg.len > 0 && msg.data != NULL)
                {
                    metadata.dst = msg.dest;
                    metadata.src = msg.src;
                    msg.metadata = metadata;
                    recvQ_enqueue(msg);
                }
            }
            else // Forward
            {
                // Loop detection logic
                if ((src == config.self || src == parentAddr) && metadata.prev != loopyParent)
                {
                    loopyParent = (src == config.self) ? metadata.prev : parentAddr; // To skip duplicate loop detection
                    printf("%s - Loop detected %02d\n", timestamp(), loopyParent);
                    // if (config.self > metadata.prev) // To avoid both nodes changing parents
                    if (1) // Always change parent
                    {
                        changeParent();
                    }
                    else
                    {
                        if (config.loglevel >= DEBUG)
                        {
                            printf("# %s - Skipping parent change... loopyParent:%02d\n", timestamp(), loopyParent);
                        }
                    }
                }
                if (MAC_send(config.mac, parentAddr, pkt, pktSize))
                {
                    printf("%s - FWD: %02d -> %02d total: %02d\n", timestamp(), src, parentAddr, ++total[src]);
                }
                else
                {
                    printf("# %s - Error FWD: %02d -> %02d\n", timestamp(), src, parentAddr);
                }
            }
        }
        else if (ctrl == CTRL_BCN)
        {
            Beacon *beacon = (Beacon *)pkt;
            if (config.loglevel >= DEBUG)
            {
                printf("# %s - Beacon src: %02d (%d) parent: %02d(%d)\n", timestamp(), metadata.prev, metadata.RSSI, beacon->parent, beacon->parentRSSI);
            }
            updateActiveNodes(metadata.prev, metadata.RSSI, beacon->parent, beacon->parentRSSI);
            metrics.data[metadata.prev].beaconsRecv++;
        }
        else
        {
            if (config.loglevel >= DEBUG)
            {
                printf("# %s - STRP : Unknown control flag %02d \n", timestamp(), ctrl);
            }
        }
        free(pkt);
        fflush(stdout);
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

    // Extract Sequence id
    uint16_t seqId;
    memcpy(&seqId, pkt, sizeof(seqId));
    pkt += sizeof(seqId);

    if (seqId <= recvSeq[msg.src] && recvSeq[msg.src] != 0)
    {
        msg.len = 0;
        msg.data = NULL;
        return msg;
    }
    recvSeq[msg.src] = seqId;

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

    // Set source as config.self
    *p = config.self;
    p += sizeof(config.self);

    // Set Sequence id
    sendSeq[msg.dest]++;
    memcpy(p, &sendSeq[msg.dest], sizeof(sendSeq[msg.dest]));
    p += sizeof(sendSeq[msg.dest]);

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
        if (!MAC_send(config.mac, parentAddr, pkt, pktSize))
        {
            printf("%s - ### Error: MAC_send failed %s:%d\n", timestamp(), __FILE__, __LINE__);
        }
        else
        {
            if (config.loglevel >= TRACE)
            {
                logMessage(TRACE, "STRP - %s: ", __func__);
                for (int i = 0; i < headerSize; i++)
                    printf("%02X ", pkt[i]);
                printf("|");
                for (int i = headerSize; i < pktSize; i++)
                    printf(" %02X", pkt[i]);
                printf("\n");
                fflush(stdout);
            }
        }
        free(pkt);
        usleep(randInRange(500000, 1200000)); // Sleep 1s
    }
    return NULL;
}

static int serializePacket(DataPacket msg, uint8_t **routePkt)
{
    uint16_t routePktSize = msg.len + headerSize;
    *routePkt = (uint8_t *)malloc(routePktSize);
    if (*routePkt == NULL)
    {
        return -1;
    }

    uint8_t *p = *routePkt;
    *p = msg.ctrl;
    p += sizeof(msg.ctrl);

    // Set dest
    *p = msg.dest;
    p += sizeof(msg.dest);

    // Set source as config.self
    *p = config.self;
    p += sizeof(config.self);

    // Set Sequence id
    sendSeq[msg.dest]++;
    memcpy(p, &sendSeq[msg.dest], sizeof(sendSeq[msg.dest]));
    p += sizeof(sendSeq[msg.dest]);

    // Set actual msg length
    memcpy(p, &msg.len, sizeof(msg.len));
    p += sizeof(msg.len);

    // Set msg
    memcpy(p, msg.data, msg.len);
    free(msg.data);
    return routePktSize;
}

static void senseNeighbours()
{
    if (config.strategy != FIXED)
    {
        parentAddr = INITIAL_PARENT;
        neighbours.nodes[parentAddr].RSSI = MIN_RSSI;
    }

    // Disable ambient noise monitoring for sensing
    MAC tempMac = *config.mac;
    config.mac->ambient = 0;

    do
    {
        uint16_t count = 0;
        if (config.loglevel >= DEBUG)
        {
            printf("# %s - Sending beacons...\n", timestamp());
            fflush(stdout);
        }

        time_t start = time(NULL);
        do
        {
            usleep(randInRange(500000, 1200000));
            sendBeacon();
            count++;
        } while (time(NULL) - start < config.senseDurationS);

        if (config.loglevel >= DEBUG)
        {
            printf("# %s - Sent %d beacons...\n", timestamp(), count);
            fflush(stdout);
        }

        if (config.self != ADDR_SINK)
        {
            if (parentAddr == INITIAL_PARENT)
            {
                printf("%s - No neighbors detected.Trying again...\n", timestamp());
            }
            else if (neighbours.nodes[parentAddr].RSSI == MIN_RSSI)
            {
                // // Strategy = FIXED
                // // Exit if assigned parent node not a neighbor
                logMessage(ERROR, "STRP: Parent node %02d not a neighbor\n", parentAddr);
                exit(EXIT_FAILURE);
            }
            else
            {
                break;
            }
        }
        else
        {
            break;
        }

    } while (1);

    // Reset ambient noise monitoring
    config.mac->ambient = tempMac.ambient;

    if (config.self != ADDR_SINK)
    {
        printf("%s - Parent: %02d (%02d)\n", timestamp(), parentAddr, neighbours.nodes[parentAddr].RSSI);
    }
    // if (config.loglevel >= DEBUG)
    {
        logMessage(DEBUG, "-------------\n");
        logMessage(DEBUG, "Active neighbors: %d\n", neighbours.numActive);
        for (uint8_t i = neighbours.minAddr; i <= neighbours.maxAddr; i++)
        {
            NodeInfo node = neighbours.nodes[i];
            if (node.state != UNKNOWN)
            {
                logMessage(DEBUG, " %02d (%d)\n", i, node.RSSI);
            }
        }
        logMessage(DEBUG, "-------------\n");
        fflush(stdout);
    }
}

static void updateActiveNodes(uint8_t addr, int8_t RSSI, uint8_t parent, int8_t parentRSSI)
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
    if (addr == parentAddr)
    {
        nodePtr->link = OUTBOUND;
    }
    else if (parent == config.self)
    {
        nodePtr->link = INBOUND;
        child = true;
    }
    else
    {
        nodePtr->link = IDLE;
    }
    if (parent != ADDR_BROADCAST)
    {
        nodePtr->parent = parent;
        nodePtr->parentRSSI = parentRSSI;
    }
    nodePtr->RSSI = RSSI;
    nodePtr->lastSeen = time(NULL);
    sem_post(&neighbours.mutex);
    if (child && parentAddr == addr && addr < config.self)
    {
        printf("%s - Direct loop with %02d..\n", timestamp(), addr);
        // loopyParent = addr;
        changeParent();
    }

    if (new)
    {
        if (config.loglevel >= DEBUG)
        {
            printf("# %s - New %s: %02d (%02d)\n", timestamp(), child ? "child" : "neighbour", addr, RSSI);
            printf("# %s - Active neighbour count: %0d\n", timestamp(), numActive);
        }

        // change parent if new neighbour fits
        if (config.strategy != FIXED && config.self != ADDR_SINK && !child && addr != parentAddr)
        {
            bool changed = false;
            uint8_t prevParentAddr = parentAddr;
            if (config.strategy == NEXT_LOWER && addr > parentAddr && addr < config.self)
            {
                parentAddr = addr;
                changed = true;
            }
            if (config.strategy == RANDOM && (rand() % 101) < 50)
            {
                parentAddr = addr;
                changed = true;
            }
            if (config.strategy == RANDOM_LOWER && addr < config.self && (rand() % 101) < 50)
            {
                parentAddr = addr;
                changed = true;
            }
            if (config.strategy == CLOSEST && RSSI > neighbours.nodes[parentAddr].RSSI)
            {
                parentAddr = addr;
                changed = true;
            }
            if (config.strategy == CLOSEST_LOWER && RSSI > neighbours.nodes[parentAddr].RSSI && addr < config.self)
            {
                parentAddr = addr;
                changed = true;
            }
            if (changed)
            {
                sem_wait(&neighbours.mutex);
                neighbours.nodes[prevParentAddr].link = IDLE;
                neighbours.nodes[addr].link = OUTBOUND;
                sem_post(&neighbours.mutex);
                if (config.loglevel >= DEBUG && prevParentAddr != INITIAL_PARENT)
                {
                    printf("# %s - Changing parent. Prev: %02d (%d) New: %02d (%d)\n", timestamp(), prevParentAddr, neighbours.nodes[prevParentAddr].RSSI, addr, RSSI);
                }
                printf("%s - Parent: %02d (%02d)\n", timestamp(), addr, RSSI);
                metrics.data[0].parentChanges++;
                sendBeacon();
            }
        }
    }
}

static void selectClosestNeighbour()
{
    uint8_t newParent = ADDR_SINK;
    int newParentRSSI = MIN_RSSI;

    const ActiveNodes activeNodes = neighbours;
    uint8_t numActive = activeNodes.numActive;

    for (uint8_t i = activeNodes.minAddr, active = 0; i <= activeNodes.maxAddr && active < numActive; i++)
    {
        NodeInfo node = activeNodes.nodes[i];
        if (node.state == ACTIVE)
        {
            if (node.link != INBOUND && node.RSSI > newParentRSSI && node.addr != parentAddr)
            {
                if (config.loglevel >= DEBUG)
                {
                    printf("# %s - Active: %02d (%02d)\n", timestamp(), node.addr, node.RSSI);
                }
                newParent = node.addr;
                newParentRSSI = node.RSSI;
            }
            active++;
        }
    }
    sem_wait(&neighbours.mutex);
    neighbours.nodes[newParent].link = OUTBOUND;
    neighbours.nodes[parentAddr].link = IDLE;
    sem_post(&neighbours.mutex);
    parentAddr = newParent;
}

void selectClosestLowerNeighbour()
{
    uint8_t newParent = ADDR_SINK;
    int newParentRSSI = MIN_RSSI;

    const ActiveNodes activeNodes = neighbours;
    uint8_t numActive = activeNodes.numActive;

    for (uint8_t i = activeNodes.minAddr, active = 0; i <= activeNodes.maxAddr && active < numActive; i++)
    {
        NodeInfo node = activeNodes.nodes[i];
        if (node.state == ACTIVE)
        {
            if (node.link != INBOUND && node.RSSI >= newParentRSSI && node.addr < config.self && node.addr != parentAddr)
            {
                if (config.loglevel >= DEBUG)
                {
                    printf("# %s - Active: %02d (%02d)\n", timestamp(), node.addr, node.RSSI);
                }
                newParent = node.addr;
                newParentRSSI = node.RSSI;
            }
            active++;
        }
    }
    sem_wait(&neighbours.mutex);
    neighbours.nodes[newParent].link = OUTBOUND;
    neighbours.nodes[parentAddr].link = IDLE;
    sem_post(&neighbours.mutex);
    parentAddr = newParent;
}

char *getRoutingStrategyStr()
{
    char *strategyStr;
    switch (config.strategy)
    {
    case NEXT_LOWER:
        strategyStr = "NEXT_LOWER";
        break;
    case RANDOM:
        strategyStr = "RANDOM";
        break;
    case RANDOM_LOWER:
        strategyStr = "RANDOM_LOWER";
        break;
    case CLOSEST:
        strategyStr = "CLOSEST";
        break;
    case CLOSEST_LOWER:
        strategyStr = "CLOSEST_LOWER";
        break;
    case FIXED:
        strategyStr = "FIXED";
        break;
    default:
        strategyStr = "UNKNOWN";
        break;
    }
    return strategyStr;
}

static void selectNextLowerNeighbour()
{
    uint8_t newParent = ADDR_SINK;
    int newParentRSSI = MIN_RSSI;

    const ActiveNodes activeNodes = neighbours;
    uint8_t numActive = activeNodes.numActive;

    for (uint8_t i = activeNodes.minAddr; i < config.self; i++)
    {
        NodeInfo node = activeNodes.nodes[i];
        if (node.state == ACTIVE)
        {
            if (config.loglevel >= DEBUG)
            {
                printf("# %s - Active: %02d (%02d)\n", timestamp(), node.addr, node.RSSI);
            }
            if (node.link != INBOUND && node.addr != parentAddr)
            {
                newParent = node.addr;
                newParentRSSI = node.RSSI;
            }
        }
    }
    sem_wait(&neighbours.mutex);
    neighbours.nodes[newParent].link = OUTBOUND;
    neighbours.nodes[parentAddr].link = IDLE;
    sem_post(&neighbours.mutex);

    parentAddr = newParent;
}

static void selectRandomNeighbour()
{
    uint8_t newParent = ADDR_SINK;
    const ActiveNodes activeNodes = neighbours;
    uint8_t numActive = activeNodes.numActive;
    NodeInfo pool[numActive];
    uint8_t p = 0;

    for (uint8_t i = activeNodes.minAddr, active = 0; i <= activeNodes.maxAddr && active < numActive; i++)
    {
        NodeInfo node = activeNodes.nodes[i];
        if (node.state == ACTIVE)
        {
            if (node.addr != ADDR_SINK && node.link != INBOUND && node.addr != parentAddr)
            {
                if (config.loglevel >= DEBUG)
                {
                    printf("# %s - Active: %02d (%02d)\n", timestamp(), node.addr, node.RSSI);
                }
                pool[p].addr = node.addr;
                pool[p].RSSI = node.RSSI;
                p++;
            }
            active++;
        }
    }
    if (p > 0)
    {
        uint8_t index = rand() % numActive;
        newParent = pool[index].addr;
    }

    sem_wait(&neighbours.mutex);
    neighbours.nodes[newParent].link = OUTBOUND;
    neighbours.nodes[parentAddr].link = IDLE;
    sem_post(&neighbours.mutex);

    parentAddr = newParent;
}

static void selectRandomLowerNeighbour()
{
    uint8_t newParent = ADDR_SINK;
    const ActiveNodes activeNodes = neighbours;
    uint8_t numActive = activeNodes.numActive;
    NodeInfo pool[numActive];
    uint8_t p = 0;

    for (uint8_t i = activeNodes.minAddr; i < config.self; i++)
    {
        NodeInfo node = activeNodes.nodes[i];
        if (node.state == ACTIVE)
        {
            if (node.addr != ADDR_SINK && node.link != INBOUND && node.addr < parentAddr)
            {
                if (config.loglevel >= DEBUG)
                {
                    printf("# %s - Active: %02d (%02d)\n", timestamp(), node.addr, node.RSSI);
                }
                pool[p].addr = node.addr;
                pool[p].RSSI = node.RSSI;
                p++;
            }
        }
    }

    if (p > 0)
    {
        uint8_t index = rand() % numActive;
        newParent = pool[index].addr;
    }

    sem_wait(&neighbours.mutex);
    neighbours.nodes[newParent].link = OUTBOUND;
    neighbours.nodes[parentAddr].link = IDLE;
    sem_post(&neighbours.mutex);

    parentAddr = newParent;
}

static void changeParent()
{
    time_t start = time(NULL);
    uint8_t prevParentAddr = parentAddr;
    switch (config.strategy)
    {
    case NEXT_LOWER:
        selectNextLowerNeighbour();
        break;
    case RANDOM:
        selectRandomNeighbour();
        break;
    case RANDOM_LOWER:
        selectRandomLowerNeighbour();
        break;
    case CLOSEST:
        selectClosestNeighbour();
        break;
    case CLOSEST_LOWER:
        selectClosestLowerNeighbour();
        break;
    case FIXED:
        parentAddr = config.parentAddr;
        break;
    default:
        selectNextLowerNeighbour();
        break;
    }
    printf("%s - New parent: %02d (%02d)\n", timestamp(), parentAddr, neighbours.nodes[parentAddr].RSSI);
    metrics.data[0].parentChanges++;
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
            if (parentAddr == nodePtr->addr)
            {
                parentInactive = true;
            }
            printf("%s - Node %02d inactive.\n", timestamp(), nodePtr->addr);
        }
    }
    numActive = neighbours.numActive;
    neighbours.lastCleanupTime = time(NULL);
    sem_post(&neighbours.mutex);
    if (parentInactive)
    {
        printf("%s - Parent inactive: %02d\n", timestamp(), parentAddr);
        changeParent();
    }
    else
    {
        if (config.loglevel >= DEBUG)
        {
            printf("# %s - Active neighbour count: %d\n", timestamp(), numActive);
        }
    }
}

static void sendBeacon()
{
    Beacon beacon;
    beacon.ctrl = CTRL_BCN;
    beacon.parent = parentAddr;
    beacon.parentRSSI = neighbours.nodes[parentAddr].RSSI;
    if (config.loglevel >= DEBUG)
    {
        printf("# %s - Sending beacon\n", timestamp());
    }

    if (!MAC_send(config.mac, ADDR_BROADCAST, (uint8_t *)&beacon, sizeof(Beacon)))
    {
        printf("%s - ### Error: MAC_send failed %s:%d\n", timestamp(), __FILE__, __LINE__);
    }
    else
    {
        metrics.data[0].beaconsSent++;
    }
}

static void *sendBeaconPeriodic(void *args)
{
    sleep(config.beaconIntervalS);
    while (1)
    {
        usleep(randInRange(500000, 1200000));
        sendBeacon();
        logMessage(INFO, "Sent beacon\n");
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
    case INBOUND:
        roleStr = "CHILD";
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
    return "AggParentChanges,AggBeaconsSent,TotalBeaconsRecv";
}

int Routing_getMetricsData(uint8_t *buffer, uint8_t addr)
{
    const STRP_Params data = metrics.data[addr];
    int rowlen = sprintf(buffer, "%d,%d,%d", metrics.data[0].parentChanges, metrics.data[0].beaconsSent, data.beaconsRecv);
    sem_wait(&metrics.mutex);
    metrics.data[addr] = (STRP_Params){0};
    metrics.data[0].beaconsSent = 0;
    metrics.data[0].parentChanges = 0;
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

    // Write parent info first
    NodeInfo node = activeNodes.nodes[parentAddr];
    uint8_t parentRow[100] = {0};
    uint16_t parentRowlen = 0;
    parentRowlen += snprintf(parentRow, sizeof(parentRow), "%ld,%d,%d,%d,%d,%d,%d,%d\n", (long)timestamp, src, node.addr, node.state, node.link, node.RSSI, node.parent, node.parentRSSI);
    if (offset + parentRowlen < size)
    {
        memcpy(buffer + offset, parentRow, parentRowlen);
        offset += parentRowlen;
    }
    else
    {
        logMessage(DEBUG, "Topology metrics buffer overflow\n");
    }

    // Clear timestamp to avoid unnecessary redundancy
    timestamp = 0L;

    // Write other node info
    for (uint8_t addr = min; addr <= max; addr++)
    {
        node = activeNodes.nodes[addr];
        if (node.state != UNKNOWN && addr != parentAddr)
        {
            uint8_t row[100];
            uint16_t rowlen = 0;
            rowlen += snprintf(row, sizeof(row), "%ld,%d,%d,%d,%d,%d,%d,%d\n", (long)timestamp, src, addr, node.state, node.link, node.RSSI, node.parent, node.parentRSSI);

            if (offset + rowlen < size)
            {
                memcpy(buffer + offset, row, rowlen);
                offset += rowlen;
            }
            else
            {
                logMessage(DEBUG, "Topology metrics buffer overflow\n");
                break;
            }
        }
    }
    return offset;
}

void setConfigDefaults(STRP_Config *config)
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
    if (config->strategy == 0)
    {
        config->strategy = CLOSEST;
    }
    if (config->loglevel == 0)
    {
        config->loglevel = INFO;
    }
    if (config->strategy == FIXED)
    {
        parentAddr = config->parentAddr;
    }
}

uint8_t *Routing_getTopologyHeader()
{
    return "Timestamp,Source,Address,State,LinkType,RSSI,Parent,ParentRSSI";
}