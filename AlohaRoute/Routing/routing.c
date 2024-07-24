#include <errno.h>     // errno
#include <pthread.h>   // pthread_create
#include <semaphore.h> // sem_init, sem_wait, sem_trywait, sem_timedwait
#include <stdbool.h>   // bool, true, false
#include <stdio.h>     // printf
#include <stdlib.h>    // rand, malloc, free, exit
#include <string.h>    // memcpy, strerror
#include <time.h>
#include <unistd.h> //sleep

#include "routing.h"
#include "../ALOHA/ALOHA.h"
#include "../GPIO/GPIO.h"
#include "../common.h"
#include "../util.h"

#define RoutingQueueSize 256
#define MAX_ACTIVE_NODES 32
#define NODE_TIMEOUT 60
#define MIN_RSSI -128

typedef enum ParentSelectionStrategy
{
    RANDOM_LOWER,
    RANDOM,
    NEXT_LOWER,
    CLOSEST,
    CLOSEST_LOWER
} ParentSelectionStrategy;

typedef enum NodeRole
{
    PARENT,
    CHILD,
    NODE
} NodeRole;

typedef struct Beacon
{
    uint8_t ctrl;
    uint8_t parent;
} Beacon;

typedef struct RoutingMessage
{
    uint8_t ctrl;
    uint8_t dest;
    uint8_t src;
    uint8_t parent; // Parent of the source node
    uint16_t numHops;
    uint16_t len;
    uint8_t *data;

    uint8_t prev;
    uint8_t next;

} RoutingMessage;

typedef struct RoutingQueue
{
    unsigned int begin, end;
    struct RoutingMessage packet[RoutingQueueSize];
    sem_t mutex, full, free;
} RoutingQueue;

typedef struct NodeInfo
{
    uint8_t addr;
    int RSSI;
    unsigned short hopsToSink;
    time_t lastSeen;
    NodeRole role;
    uint8_t parent;
    bool isActive;
} NodeInfo;

typedef struct ActiveNodes
{
    NodeInfo nodes[MAX_ACTIVE_NODES];
    sem_t mutex;
    unsigned short numActive;
    time_t lastCleanupTime;
} ActiveNodes;

static RoutingQueue sendQ, recvQ;
static pthread_t recvT;
static pthread_t sendT;
static MAC mac;
static uint8_t debugFlag;
static const unsigned short maxBeacons = 5;
static const unsigned short headerSize = 8; // [ctrl | dest | src | parent | numHops(2) | len(2) | [data(len)] ]
static uint8_t parentAddr;
static ActiveNodes network;
static uint8_t loopyParent;
static ParentSelectionStrategy strategy = CLOSEST_LOWER;
static const unsigned int senseDuration = 30; // duration for neighbour sensing

static void sendQ_init();
static void recvQ_init();
static void sendQ_enqueue(RoutingMessage msg);
static RoutingMessage sendQ_dequeue();
static void recvQ_enqueue(RoutingMessage msg);
static RoutingMessage recvMsgQ_dequeue();
static void *recvPackets_func(void *args);
static RoutingMessage buildRoutingMessage(uint8_t *pkt);
static void *sendPackets_func(void *args);
static int buildRoutingPacket(RoutingMessage msg, uint8_t **routePkt);
static void setDebug(uint8_t d);
static int recvMsgQ_timed_dequeue(RoutingMessage *msg, struct timespec *ts);
static void *sendBeaconHandler(void *args);
static void *receiveBeaconHandler(void *args);
static void selectParent();
static void updateActiveNodes(uint8_t addr, int rssi, uint8_t parent);
static void changeParent();
static void initActiveNodes();
static void cleanupInactiveNodes();
static void selectRandomLowerNeighbour();
static void selectRandomNeighbour();
static void selectNextLowerNeighbour();
static void selectClosestNeighbour();
static void selectClosestLowerNeighbour();
static void printRoutingStrategy();
static void sendBeacon();

// Initialize the routing layer
int routingInit(uint8_t self, uint8_t debug, unsigned int timeout)
{
    setDebug(debug);
    srand(self * time(NULL));
    MAC_init(&mac, self);
    mac.debug = 0;
    mac.recvTimeout = timeout;
    sendQ_init();
    recvQ_init();
    initActiveNodes();
    if (pthread_create(&recvT, NULL, recvPackets_func, &mac) != 0)
    {
        printf("## Error: Failed to create Routing receive thread");
        exit(1);
    }
    if (self != ADDR_SINK)
    {
        printRoutingStrategy();
        selectParent();
    }

    if (self != ADDR_SINK)
    {
        if (pthread_create(&sendT, NULL, sendPackets_func, &mac) != 0)
        {
            printf("## Error: Failed to create Routing send thread");
            exit(1);
        }
    }
    return 1;
}

// Send a message via the routing layer
int routingSend(uint8_t dest, uint8_t *data, unsigned int len)
{
    RoutingMessage msg;
    msg.ctrl = CTRL_PKT;
    msg.dest = dest;
    msg.src = mac.addr;
    msg.len = len;
    msg.next = parentAddr;
    msg.parent = parentAddr;
    msg.numHops = 0;
    msg.data = (uint8_t *)malloc(len);
    if (msg.data != NULL)
    {
        memcpy(msg.data, data, len);
    }
    else
    {
        if (debugFlag)
        {
            printf("%s - ## Error: msg.data is NULL %s:%s\n", timestamp(), __FILE__, __LINE__);
        }
    }
    memcpy(msg.data, data, len);
    sendQ_enqueue(msg);
    return 1;
}

// Receive a message via the routing layer
int routingReceive(RouteHeader *header, uint8_t *data)
{
    RoutingMessage msg = recvMsgQ_dequeue();
    header->dst = msg.dest;
    header->RSSI = mac.RSSI;
    header->src = msg.src;
    header->numHops = msg.numHops;
    header->prev = mac.recvH.src_addr;
    if (msg.data != NULL)
    {
        memcpy(data, msg.data, msg.len);
    }
    else
    {
        if (debugFlag)
        {
            printf("%s - ## Error: msg.data is NULL %s:%s\n", timestamp(), __FILE__, __LINE__);
        }
    }

    return msg.len;
}

// Receive a message via the routing layer
// Returns 0 for timeout, -1 for error
int routingTimedReceive(RouteHeader *header, uint8_t *data, unsigned int timeout)
{

    RoutingMessage msg;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout;

    int result = recvMsgQ_timed_dequeue(&msg, &ts);
    if (result <= 0)
    {
        return result;
    }

    // Populate header with message details
    header->dst = msg.dest;
    header->RSSI = mac.RSSI;
    header->src = msg.src;
    header->numHops = msg.numHops;
    header->prev = mac.recvH.src_addr;

    if (msg.data != NULL)
    {
        memcpy(data, msg.data, msg.len);
    }
    else
    {
        if (debugFlag)
        {
            printf("%s - ## Error: msg.data is NULL %s:%d\n", timestamp(), __FILE__, __LINE__);
        }
    }

    return msg.len;
}

static int recvMsgQ_timed_dequeue(RoutingMessage *msg, struct timespec *ts)
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
    recvQ.begin = (recvQ.begin + 1) % RoutingQueueSize;
    sem_post(&recvQ.mutex);
    sem_post(&recvQ.free);
    return 1;
}

static void sendQ_init()
{
    sendQ.begin = 0;
    sendQ.end = 0;
    sem_init(&sendQ.full, 0, 0);
    sem_init(&sendQ.free, 0, RoutingQueueSize);
    sem_init(&sendQ.mutex, 0, 1);
}

static void recvQ_init()
{
    recvQ.begin = 0;
    recvQ.end = 0;
    sem_init(&recvQ.full, 0, 0);
    sem_init(&recvQ.free, 0, RoutingQueueSize);
    sem_init(&recvQ.mutex, 0, 1);
}

static void sendQ_enqueue(RoutingMessage msg)
{
    sem_wait(&sendQ.free);
    sem_wait(&sendQ.mutex);
    sendQ.packet[sendQ.end] = msg;
    sendQ.end = (sendQ.end + 1) % RoutingQueueSize;
    sem_post(&sendQ.mutex);
    sem_post(&sendQ.full);
    // int freeCount, fullCount;
    // sem_getvalue(&sendQ.free, &freeCount);
    // sem_getvalue(&sendQ.full, &fullCount);
    // if (debugFlag)
    // {
    //     printf("%s - sendQ_enqueue: src=%d, dest=%d, free=%d, full=%d\n", timestamp(), msg.src, msg.dest, freeCount, fullCount);
    // }
}

static RoutingMessage sendQ_dequeue()
{
    sem_wait(&sendQ.full);
    sem_wait(&sendQ.mutex);
    RoutingMessage msg = sendQ.packet[sendQ.begin];
    sendQ.begin = (sendQ.begin + 1) % RoutingQueueSize;
    sem_post(&sendQ.mutex);
    sem_post(&sendQ.free);
    // int freeCount, fullCount;
    // sem_getvalue(&sendQ.free, &freeCount);
    // sem_getvalue(&sendQ.full, &fullCount);
    // if (debugFlag)
    // {
    //     printf("%s - sendQ_dequeue: src=%d, dest=%d, free=%d, full=%d\n", timestamp(), msg.src, msg.dest, freeCount, fullCount);
    // }
    return msg;
}

static void recvQ_enqueue(RoutingMessage msg)
{
    sem_wait(&recvQ.free);
    sem_wait(&recvQ.mutex);
    recvQ.packet[recvQ.end] = msg;
    recvQ.end = (recvQ.end + 1) % RoutingQueueSize;
    sem_post(&recvQ.mutex);
    sem_post(&recvQ.full);
    // int freeCount, fullCount;
    // sem_getvalue(&recvQ.free, &freeCount);
    // sem_getvalue(&recvQ.full, &fullCount);
    // if (debugFlag)
    // {
    //     printf("%s - recvQ_enqueue: src=%d, dest=%d, free=%d, full=%d\n", timestamp(), msg.src, msg.dest, freeCount, fullCount);
    // }
}

static RoutingMessage recvMsgQ_dequeue()
{
    sem_wait(&recvQ.full);
    sem_wait(&recvQ.mutex);
    RoutingMessage msg = recvQ.packet[recvQ.begin];
    recvQ.begin = (recvQ.begin + 1) % RoutingQueueSize;
    sem_post(&recvQ.mutex);
    sem_post(&recvQ.free);
    // int freeCount, fullCount;
    // sem_getvalue(&recvQ.free, &freeCount);
    // sem_getvalue(&recvQ.full, &fullCount);
    // if (debugFlag)
    // {
    //     printf("%s - recvMsgQ_dequeue: src=%d, dest=%d, free=%d, full=%d\n", timestamp(), msg.src, msg.dest, freeCount, fullCount);
    // }
    return msg;
}

static void *recvPackets_func(void *args)
{
    unsigned int total[25] = {0};
    MAC *macTemp = (MAC *)args;
    time_t start = time(NULL);
    time_t current;
    while (1)
    {
        uint8_t *pkt = (uint8_t *)malloc(240);
        if (pkt == NULL)
        {
            free(pkt);
            continue;
        }
        // int pktSize = MAC_recv(macTemp, pkt);
        int pktSize = MAC_timedrecv(macTemp, pkt, 1);
        if (pktSize == 0)
        {
            free(pkt);
            continue;
        }
        uint8_t ctrl = *pkt;
        if (ctrl == CTRL_PKT)
        {
            RoutingMessage msg = buildRoutingMessage(pkt);

            updateActiveNodes(mac.recvH.src_addr, mac.RSSI, ADDR_BROADCAST);
            if (msg.dest == ADDR_BROADCAST || msg.dest == macTemp->addr)
            {
                // Keep
                recvQ_enqueue(msg);
            }
            else // Forward
            {
                if (msg.src == mac.addr && mac.recvH.src_addr != loopyParent)
                {
                    loopyParent = mac.recvH.src_addr; // To skip duplicate loop detection
                    printf("%s - Loop detected %02d (%02d) msg: %s\n", timestamp(), loopyParent, msg.numHops, msg.data);
                    if (mac.addr > mac.recvH.src_addr) // To avoid both nodes changing parents
                    {
                        changeParent();
                    }
                    else
                    {
                        if (debugFlag)
                        {
                            printf("%s - Skipping parent change...\n", timestamp(), loopyParent, msg.numHops, msg.data);
                        }
                    }
                }
                msg.next = parentAddr;

                if (MAC_send(macTemp, msg.next, pkt, pktSize))
                {
                    printf("%s - FWD: %02d (%02d) -> %02d msg: %s total: %02d\n", timestamp(), msg.src, msg.numHops, msg.next, msg.data, ++total[msg.src]);
                }
                else
                {
                    if (debugFlag)
                    {
                        printf("%s - ## Error FWD: %02d (%02d) -> %02d msg: %s\n", timestamp(), msg.src, msg.numHops, msg.next, msg.data);
                    }
                }

                if (msg.data != NULL)
                {
                    free(msg.data);
                }
            }
        }
        else if (ctrl == CTRL_BCN)
        {
            Beacon *beacon = (Beacon *)pkt;
            printf("### %s - Beacon src: %02d parent: %02d\n", timestamp(), mac.recvH.src_addr, beacon->parent);
            updateActiveNodes(mac.recvH.src_addr, mac.RSSI, beacon->parent);
        }
        else
        {
            if (debugFlag)
            {
                printf("%s - ## Routing : Unknown control flag %02d \n", timestamp(), ctrl);
            }
        }
        free(pkt);
        if (0)
        {
            current = time(NULL);
            if (current - start > 43)
            {
                printf("### %s - Sending beacon\n", timestamp());
                sendBeacon();
                start = current;
            }
        }
        usleep(rand() % 1000);
    }
}

// Construct RoutingMessage
static RoutingMessage buildRoutingMessage(uint8_t *pkt)
{
    RoutingMessage msg;
    msg.ctrl = *pkt;
    pkt += sizeof(msg.ctrl);

    msg.dest = *pkt;
    pkt += sizeof(msg.dest);

    msg.src = *pkt;
    pkt += sizeof(msg.src);

    msg.parent = *pkt;
    pkt += sizeof(msg.parent);

    msg.prev = mac.recvH.src_addr;

    uint16_t numHops;
    memcpy(&numHops, pkt, sizeof(msg.numHops));
    msg.numHops = ++numHops;
    memcpy(pkt, &numHops, sizeof(msg.numHops));
    pkt += sizeof(msg.numHops);

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

static void *sendPackets_func(void *args)
{
    MAC *macTemp = (MAC *)args;
    while (1)
    {
        RoutingMessage msg = sendQ_dequeue();
        uint8_t *pkt;
        unsigned int pktSize = buildRoutingPacket(msg, &pkt);
        if (pkt == NULL)
        {
            free(pkt);
            continue;
        }
        if (!MAC_send(macTemp, parentAddr, pkt, pktSize))
        {
            printf("%s - ## Error: MAC_send failed %s:%s\n", timestamp(), __FILE__, __LINE__);
        }
        free(pkt);
        usleep(1000);
    }
}

static int buildRoutingPacket(RoutingMessage msg, uint8_t **routePkt)
{
    uint16_t routePktSize = msg.len + headerSize;
    *routePkt = (uint8_t *)malloc(routePktSize);

    uint8_t *p = *routePkt;
    *p = msg.ctrl;
    p += sizeof(msg.ctrl);

    // Set dest
    *p = msg.dest;
    p += sizeof(msg.dest);

    // Set source as self
    *p = mac.addr;
    p += sizeof(mac.addr);

    // Set parent
    *p = parentAddr;
    p += sizeof(parentAddr);

    // Set numHops
    *p = msg.numHops;
    p += sizeof(msg.numHops);

    // Set actual msg length
    *p = msg.len;
    p += sizeof(msg.len);

    // Set msg
    memcpy(p, msg.data, msg.len);
    free(msg.data);
    return routePktSize;
}

static void setDebug(uint8_t d)
{
    debugFlag = d;
}

static void *sendBeaconHandler(void *args)
{

    MAC *m = (MAC *)args;
    int trials = 0;
    if (debugFlag)
    {
        printf("%s - ## Sending beacons...\n", timestamp());
    }
    Beacon beacon;
    beacon.ctrl = CTRL_BCN;
    beacon.parent = parentAddr;

    uint8_t *data = (uint8_t *)&beacon;
    unsigned int dataSize = sizeof(beacon);
    time_t start = time(NULL);
    time_t current;
    do
    {
        if (MAC_send(m, ADDR_BROADCAST, data, dataSize))
        {
            trials++;
        }
        sleep(1);
        current = time(NULL);
    } while (current - start < senseDuration);

    if (debugFlag)
    {
        printf("%s - ## Sent %d beacons...\n", timestamp(), trials);
    }
    return NULL;
}

static void *receiveBeaconHandler(void *args)
{
    MAC *m = (MAC *)args;
    int trials = 0;
    if (debugFlag)
    {
        printf("%s - ## Listening for beacons...\n", timestamp());
    }
    time_t start = time(NULL);
    time_t current;
    do
    {
        unsigned char data[sizeof(Beacon)];
        int len = MAC_timedrecv(m, (unsigned char *)&data, 1);
        if (len > 0)
        {
            Beacon *beacon = (Beacon *)data;
            if (beacon->ctrl == CTRL_BCN)
            {
                uint8_t addr = m->recvH.src_addr;
                int rssi = m->RSSI;
                updateActiveNodes(addr, rssi, beacon->parent);
                trials++;
            }
        }
        usleep(rand() % 1000);
        current = time(NULL);
    } while (current - start <= senseDuration);
    if (debugFlag)
    {
        printf("%s - ## Received %d beacons...\n", timestamp(), trials);
    }
    return NULL;
}

static void selectParent()
{
    pthread_t send, recv;
    parentAddr = ADDR_SINK;
    network.nodes[parentAddr].RSSI = MIN_RSSI;

    if (pthread_create(&send, NULL, sendBeaconHandler, &mac) != 0)
    {
        printf("Failed to create beacon send thread");
        exit(1);
    }

    // if (pthread_create(&recv, NULL, receiveBeaconHandler, &mac) != 0)
    // {
    //     printf("Failed to create beacon receive thread");
    //     exit(1);
    // }
    // sleep(senseDuration + 1);
    // pthread_join(recv, NULL);
    pthread_join(send, NULL);
    sleep(5);
    printf("%s - Parent: %02d (%02d)\n", timestamp(), parentAddr, network.nodes[parentAddr].RSSI);
}

void updateActiveNodes(uint8_t addr, int RSSI, uint8_t parent)
{
    sem_wait(&network.mutex);
    NodeInfo *node = &network.nodes[addr];
    unsigned short numActive;
    bool new = !node->isActive;
    bool child = false;
    if (new)
    {
        node->addr = addr;
        node->isActive = true;
        network.numActive++;
        numActive = network.numActive;
    }
    if (addr == parentAddr)
    {
        node->role = PARENT;
    }
    else if (parent == mac.addr)
    {
        node->role = CHILD;
        child = true;
    }
    else
    {
        node->role = NODE;
    }
    if (parent != ADDR_BROADCAST)
    {
        node->parent = parent;
    }
    node->RSSI = RSSI;
    node->lastSeen = time(NULL);
    sem_post(&network.mutex);
    if (child && parentAddr == addr && addr < mac.addr)
    {
        printf("%s - Direct loop with %02d..\n", timestamp(), addr);
        // loopyParent = addr;
        changeParent();
    }

    if (new)
    {
        if (debugFlag)
        {
            printf("%s - ##  New %s: %02d (%02d)\n", timestamp(), child ? "child" : "neighbour", addr);
            printf("%s - ##  Active neighbour count: %0d\n", timestamp(), numActive);
        }
    }
    // change parent if new neighbour fits
    if (mac.addr != ADDR_SINK && !child && addr != parentAddr)
    {
        bool changed = false;
        uint8_t prevParentAddr = parentAddr;
        if (strategy == NEXT_LOWER && addr > parentAddr && addr < mac.addr)
        {
            parentAddr = addr;
            changed = true;
        }
        if (strategy == RANDOM && (rand() % 101) < 50)
        {
            parentAddr = addr;
            changed = true;
        }
        if (strategy == RANDOM_LOWER && addr < mac.addr && (rand() % 101) < 50)
        {
            parentAddr = addr;
            changed = true;
        }
        if (strategy == CLOSEST && RSSI > network.nodes[parentAddr].RSSI)
        {
            parentAddr = addr;
            changed = true;
        }
        if (strategy == CLOSEST_LOWER && RSSI >= network.nodes[parentAddr].RSSI && addr < mac.addr)
        {
            parentAddr = addr;
            changed = true;
        }
        if (changed)
        {
            sem_wait(&network.mutex);
            network.nodes[prevParentAddr].role = NODE;
            network.nodes[addr].role = PARENT;
            sem_post(&network.mutex);
            printf("%s - Parent: %02d (%02d)\n", timestamp(), addr, RSSI);
        }
    }
}

static void selectClosestNeighbour()
{
    uint8_t newParent = ADDR_SINK;
    unsigned short numActive = network.numActive;
    int newParentRSSI = MIN_RSSI;

    sem_wait(&network.mutex);
    for (int i = 0, active = 0; active < numActive && i < MAX_ACTIVE_NODES; i++)
    {
        NodeInfo node = network.nodes[i];
        if (node.isActive)
        {
            if (node.role != CHILD && node.RSSI > newParentRSSI)
            {
                if (debugFlag)
                {
                    printf("%s - ##  Active: %02d (%02d)\n", timestamp(), node.addr, node.RSSI);
                }
                newParent = node.addr;
                newParentRSSI = node.RSSI;
            }
            active++;
        }
    }
    sem_post(&network.mutex);
    parentAddr = newParent;
}

void selectClosestLowerNeighbour()
{
    uint8_t newParent = ADDR_SINK;
    unsigned short numActive = network.numActive;
    int newParentRSSI = MIN_RSSI;

    sem_wait(&network.mutex);
    for (int i = 0, active = 0; active < numActive && i < MAX_ACTIVE_NODES; i++)
    {
        NodeInfo node = network.nodes[i];
        if (node.isActive)
        {
            if (node.role != CHILD && node.RSSI >= newParentRSSI && node.addr < mac.addr)
            {
                if (debugFlag)
                {
                    printf("%s - ##  Active: %02d (%02d)\n", timestamp(), node.addr, node.RSSI);
                }
                newParent = node.addr;
                newParentRSSI = node.RSSI;
            }
            active++;
        }
    }
    sem_post(&network.mutex);
    parentAddr = newParent;
}

void printRoutingStrategy()
{
    const char *strategyStr;
    switch (strategy)
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
    default:
        strategyStr = "UNKNOWN";
        break;
    }
    printf("%s - Routing strategy: %s\n", timestamp(), strategyStr);
}

static void selectNextLowerNeighbour()
{
    uint8_t newParent = ADDR_SINK;
    int newParentRSSI = MIN_RSSI;

    sem_wait(&network.mutex);
    for (int i = 0; i < mac.addr; i++)
    {
        NodeInfo node = network.nodes[i];
        if (node.isActive)
        {
            if (debugFlag)
            {
                printf("%s - ##  Active: %02d (%02d)\n", timestamp(), node.addr, node.RSSI);
            }
            if (node.role != CHILD)
            {
                newParent = node.addr;
                newParentRSSI = node.RSSI;
            }
        }
    }
    sem_post(&network.mutex);

    parentAddr = newParent;
}

static void selectRandomNeighbour()
{
    uint8_t newParent = ADDR_SINK;
    unsigned short numActive = network.numActive;
    NodeInfo pool[numActive];
    int newParentRSSI = MIN_RSSI;
    int p = 0;

    sem_wait(&network.mutex);
    for (int i = 0, active = 0; active < numActive && i < MAX_ACTIVE_NODES; i++)
    {
        NodeInfo node = network.nodes[i];
        if (node.isActive)
        {
            if (node.addr != ADDR_SINK && node.role != CHILD && node.addr != parentAddr)
            {
                if (debugFlag)
                {
                    printf("%s - ##  Active: %02d (%02d)\n", timestamp(), node.addr, node.RSSI);
                }
                pool[p].addr = node.addr;
                pool[p].RSSI = node.RSSI;
                p++;
            }
            active++;
        }
    }
    sem_post(&network.mutex);
    if (p == 0)
    {
        newParent = ADDR_SINK;
    }
    else
    {
        uint8_t index = rand() % numActive;
        newParent = pool[index].addr;
        newParentRSSI = pool[index].addr;
    }

    parentAddr = newParent;
}

static void selectRandomLowerNeighbour()
{
    uint8_t newParent = ADDR_SINK;
    unsigned short numActive = network.numActive;
    NodeInfo pool[numActive];
    int newParentRSSI = MIN_RSSI;
    int p = 0;

    sem_wait(&network.mutex);
    for (int i = 0; i < mac.addr; i++)
    {
        NodeInfo node = network.nodes[i];
        if (node.isActive)
        {
            if (node.addr != ADDR_SINK && node.role != CHILD && node.addr < parentAddr)
            {
                if (debugFlag)
                {
                    printf("%s - ##  Active: %02d (%02d)\n", timestamp(), node.addr, node.RSSI);
                }
                pool[p].addr = node.addr;
                pool[p].RSSI = node.RSSI;
                p++;
            }
        }
    }
    sem_post(&network.mutex);

    if (p == 0)
    {
        newParent = ADDR_SINK;
    }
    else
    {
        uint8_t index = rand() % numActive;
        newParent = pool[index].addr;
        newParentRSSI = pool[index].addr;
    }

    parentAddr = newParent;
}

static void changeParent()
{
    uint8_t prevParentAddr = parentAddr;
    switch (strategy)
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
    default:
        selectNextLowerNeighbour();
        break;
    }
    sem_wait(&network.mutex);
    network.nodes[prevParentAddr].role = NODE;
    network.nodes[parentAddr].role = PARENT;
    sem_post(&network.mutex);
    printf("%s - New parent: %02d (%02d)\n", timestamp(), parentAddr, network.nodes[parentAddr].RSSI);
}

void initActiveNodes()
{
    sem_init(&network.mutex, 0, 1);
    network.numActive = 0;
    memset(network.nodes, 0, sizeof(network.nodes));
}

static void cleanupInactiveNodes()
{
    time_t currentTime = time(NULL);
    bool parentInactive = false;
    sem_wait(&network.mutex);
    unsigned short numActive = network.numActive;
    for (int i = 0, active = 0; i < MAX_ACTIVE_NODES && active < numActive; i++)
    {
        NodeInfo node = network.nodes[i];
        if (node.isActive && (currentTime - node.lastSeen) > NODE_TIMEOUT)
        {
            NodeInfo *ptr = &node;
            ptr->isActive = false;
            ptr->role = NODE;
            network.numActive--;
            active++;
            if (parentAddr == node.addr)
            {
                parentInactive = true;
            }

            if (debugFlag)
            {
                printf("%s - ## Inactive: %02d\n", timestamp(), node.addr);
            }
        }
    }
    numActive = network.numActive;
    sem_post(&network.mutex);
    if (parentInactive)
    {
        if (debugFlag)
        {
            printf("%s - ##  Parent inactive: %02d\n", timestamp(), parentAddr);
        }
        changeParent();
    }
    else
    {
        if (debugFlag)
        {
            printf("%s - ##  Active neighbour count: %d\n", timestamp(), numActive);
        }
    }
    network.lastCleanupTime = time(NULL);
}

static void sendBeacon()
{
    Beacon beacon;
    beacon.ctrl = CTRL_BCN;
    beacon.parent = parentAddr;
    MAC_Isend(&mac, ADDR_BROADCAST, (uint8_t *)&beacon, sizeof(Beacon));
}
