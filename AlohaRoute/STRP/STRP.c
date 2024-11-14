#include <errno.h>     // errno
#include <pthread.h>   // pthread_create
#include <semaphore.h> // sem_init, sem_wait, sem_trywait, sem_timedwait
#include <stdbool.h>   // bool, true, false
#include <stdio.h>     // printf
#include <stdlib.h>    // rand, malloc, free, exit
#include <string.h>    // memcpy, strerror
#include <time.h>
#include <unistd.h>   // sleep, exec
#include <signal.h>   // signal
#include <sys/wait.h> // waitpid
#include <execinfo.h> // backtrace

#include "STRP.h"
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
    int parentRSSI;
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
    int parentRSSI;
} NodeInfo;

typedef struct ActiveNodes
{
    NodeInfo nodes[MAX_ACTIVE_NODES];
    sem_t mutex;
    uint8_t numActive;
    time_t lastCleanupTime;
    uint8_t minAddr, maxAddr;
} ActiveNodes;

typedef struct NodeRoutingTable
{
    char *timestamp;
    uint8_t src;
    uint8_t numActive;
    NodeInfo nodes[MAX_ACTIVE_NODES];
} NodeRoutingTable;

typedef struct RoutingTables
{
    NodeRoutingTable table[MAX_ACTIVE_NODES];
    sem_t mutex, full, free;
    unsigned int begin, end;
} RoutingTables;

static RoutingQueue sendQ, recvQ;
static pthread_t recvT;
static pthread_t sendT;
static MAC mac;
static LogLevel loglevel;
static const unsigned short headerSize = 8; // [ ctrl | dest | src | parent | numHops[2] | len[2] | [data[len] ]
static uint8_t parentAddr;
static ActiveNodes neighbours;
static uint8_t loopyParent;
static ParentSelectionStrategy strategy = CLOSEST_LOWER;
static const unsigned int senseDuration = 30;        // duration for neighbour sensing
static const unsigned int beaconInterval = 31;       // Interval between periodic beacons
static const unsigned int routingTableInterval = 30; // Interval to sending routing table
static RoutingTables routingTables;
static const char *outputCSV = "/home/pi/sw_workspace/AlohaRoute/Debug/results/network.csv";
static pid_t serverPid;

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
static void setDebug(LogLevel d);
static int recvMsgQ_timed_dequeue(RoutingMessage *msg, struct timespec *ts);
static void *sendBeaconHandler(void *args);
static void *receiveBeaconHandler(void *args);
static void selectParent();
static void updateActiveNodes(uint8_t addr, int RSSI, uint8_t parent, int parentRSSI);
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
static void *sendBeaconPeriodic(void *args);
static void *sendRoutingTable(void *args);
static RoutingMessage buildRoutingTableMsg();
static void parseRoutingTablePkt(RoutingMessage tab);
static char *getRoleStr(NodeRole role);
static void routingTables_init();
static void routingTables_enqueue(NodeRoutingTable table);
static NodeRoutingTable routingTables_dequeue();
static void *saveRoutingTable(void *args);
static void createCSVFile();
static void createHttpServer();
static void signalHandler(int signum);
static int routingTables_timed_dequeue(NodeRoutingTable *tab, struct timespec *ts);
static void installDependencies();
static void writeToCSVFile(NodeRoutingTable table);

// Initialize the routing layer
int routingInit(uint8_t self, uint8_t debug, unsigned int timeout)
{
    pthread_t sendBeaconT;
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
        exit(EXIT_FAILURE);
    }
    if (self != ADDR_SINK)
    {
        printRoutingStrategy();
        selectParent();
    }
    else
    {
        sendBeacon();
    }

    if (self != ADDR_SINK)
    {
        if (pthread_create(&sendT, NULL, sendPackets_func, &mac) != 0)
        {
            printf("## Error: Failed to create Routing send thread");
            exit(EXIT_FAILURE);
        }
    }
    // Beacon thread
    if (pthread_create(&sendBeaconT, NULL, sendBeaconPeriodic, &mac) != 0)
    {
        printf("## Error: Failed to create sendBeaconPeriodic thread");
        exit(EXIT_FAILURE);
    }
    // Routingtable thread
    if (self != ADDR_SINK)
    {
        pthread_t sendRoutingTableT;
        if (pthread_create(&sendRoutingTableT, NULL, sendRoutingTable, &mac) != 0)
        {
            printf("## Error: Failed to create sendRoutingTable thread");
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        pthread_t saveRoutingTableT;
        routingTables_init();
        createCSVFile();
        installDependencies();
        createHttpServer();
        if (pthread_create(&saveRoutingTableT, NULL, saveRoutingTable, &mac) != 0)
        {
            printf("## Error: Failed to create saveRoutingTableT thread");
            exit(EXIT_FAILURE);
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
        printf("%s - ## Error: msg.data is NULL %s:%d\n", timestamp(), __FILE__, __LINE__);
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
        printf("%s - ## Error: msg.data is NULL %s:%d\n", timestamp(), __FILE__, __LINE__);
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
        printf("%s - ## Error: msg.data is NULL %s:%d\n", timestamp(), __FILE__, __LINE__);
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
}

static RoutingMessage sendQ_dequeue()
{
    sem_wait(&sendQ.full);
    sem_wait(&sendQ.mutex);
    RoutingMessage msg = sendQ.packet[sendQ.begin];
    sendQ.begin = (sendQ.begin + 1) % RoutingQueueSize;
    sem_post(&sendQ.mutex);
    sem_post(&sendQ.free);
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
}

static RoutingMessage recvMsgQ_dequeue()
{
    sem_wait(&recvQ.full);
    sem_wait(&recvQ.mutex);
    RoutingMessage msg = recvQ.packet[recvQ.begin];
    recvQ.begin = (recvQ.begin + 1) % RoutingQueueSize;
    sem_post(&recvQ.mutex);
    sem_post(&recvQ.free);
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
        if (ctrl == CTRL_PKT || ctrl == CTRL_TAB)
        {
            RoutingMessage msg = buildRoutingMessage(pkt);
            updateActiveNodes(mac.recvH.src_addr, mac.RSSI, ADDR_BROADCAST, MIN_RSSI);
            if (msg.dest == ADDR_BROADCAST || msg.dest == macTemp->addr)
            {
                if (ctrl == CTRL_TAB)
                {
                    parseRoutingTablePkt(msg);
                    free(msg.data);
                }
                else
                {
                    // Keep
                    recvQ_enqueue(msg);
                }
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
                        if (loglevel >= DEBUG)
                        {
                            printf("%s - Skipping parent change... loopyParent:%02d\n", timestamp(), loopyParent);
                        }
                    }
                }
                msg.next = parentAddr;

                if (MAC_send(macTemp, msg.next, pkt, pktSize))
                {
                    printf("%s - FWD: %02d (%02d) -> %02d total: %02d\n", timestamp(), msg.src, msg.numHops, parentAddr, ++total[msg.src]);
                }
                else
                {
                    printf("%s - ## Error FWD: %02d (%02d) -> %02d\n", timestamp(), msg.src, msg.numHops, parentAddr);
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
            if (loglevel >= DEBUG)
            {
                printf("### %s - Beacon src: %02d (%d) parent: %02d(%d)\n", timestamp(), mac.recvH.src_addr, mac.RSSI, beacon->parent, beacon->parentRSSI);
            }
            updateActiveNodes(mac.recvH.src_addr, mac.RSSI, beacon->parent, beacon->parentRSSI);
        }
        else
        {
            if (loglevel >= DEBUG)
            {
                printf("%s - ## Routing : Unknown control flag %02d \n", timestamp(), ctrl);
            }
        }
        free(pkt);
        if (0)
        {
            current = time(NULL);
            if (current - start > 33)
            {
                sendBeacon();
                start = current;
            }
        }
        usleep(rand() % 1000);
    }
    return NULL;
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
            printf("%s - ## Error: MAC_send failed %s:%d\n", timestamp(), __FILE__, __LINE__);
        }
        free(pkt);
        usleep(1000);
    }
    return NULL;
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

static void setDebug(LogLevel d)
{
    loglevel = d;
}

static void *sendBeaconHandler(void *args)
{

    MAC *m = (MAC *)args;
    int trials = 0;
    if (loglevel >= DEBUG)
    {
        printf("%s - ## Sending beacons...\n", timestamp());
    }

    time_t start = time(NULL);
    time_t current;
    do
    {
        sendBeacon();
        trials++;
        usleep(randInRange(500000, 1200000));
        current = time(NULL);
    } while (current - start < senseDuration);

    if (loglevel >= DEBUG)
    {
        printf("%s - ## Sent %d beacons...\n", timestamp(), trials);
    }
    return NULL;
}

static void *receiveBeaconHandler(void *args)
{
    MAC *m = (MAC *)args;
    int trials = 0;
    if (loglevel >= DEBUG)
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
                updateActiveNodes(addr, rssi, beacon->parent, beacon->parentRSSI);
                trials++;
            }
        }
        usleep(rand() % 1000);
        current = time(NULL);
    } while (current - start <= senseDuration);
    if (loglevel >= DEBUG)
    {
        printf("%s - ## Received %d beacons...\n", timestamp(), trials);
    }
    return NULL;
}

static void selectParent()
{
    pthread_t send, recv;
    parentAddr = ADDR_SINK;
    neighbours.nodes[parentAddr].RSSI = MIN_RSSI;

    if (pthread_create(&send, NULL, sendBeaconHandler, &mac) != 0)
    {
        printf("Failed to create beacon send thread");
        exit(EXIT_FAILURE);
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
    printf("%s - Parent: %02d (%02d)\n", timestamp(), parentAddr, neighbours.nodes[parentAddr].RSSI);
}

static void updateActiveNodes(uint8_t addr, int RSSI, uint8_t parent, int parentRSSI)
{
    if (loglevel == TRACE)
    {
        printf("### Inside updateActiveNodes addr:%02d (%d) parent:%02d(%d)\n", addr, RSSI, parent, parentRSSI);
    }
    sem_wait(&neighbours.mutex);
    NodeInfo *node = &neighbours.nodes[addr];
    uint8_t numActive;
    bool new = !node->isActive;
    bool child = false;
    if (new)
    {
        node->addr = addr;
        node->isActive = true;
        neighbours.numActive++;
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
        node->parentRSSI = parentRSSI;
    }
    node->RSSI = RSSI;
    node->lastSeen = time(NULL);
    sem_post(&neighbours.mutex);
    if (child && parentAddr == addr && addr < mac.addr)
    {
        printf("%s - Direct loop with %02d..\n", timestamp(), addr);
        // loopyParent = addr;
        changeParent();
    }

    if (new)
    {
        if (loglevel >= DEBUG)
        {
            printf("%s - ##  New %s: %02d (%02d)\n", timestamp(), child ? "child" : "neighbour", addr, RSSI);
            printf("%s - ##  Active neighbour count: %0d\n", timestamp(), numActive);
        }
    }
    // change parent if new neighbour fits
    if (mac.addr != ADDR_SINK && !child && addr != parentAddr)
    {
        printf("### Inside change block parent:%02d (%d)\n", parentAddr, neighbours.nodes[parentAddr].RSSI);
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
        if (strategy == CLOSEST && RSSI > neighbours.nodes[parentAddr].RSSI)
        {
            parentAddr = addr;
            changed = true;
        }
        if (strategy == CLOSEST_LOWER && RSSI > neighbours.nodes[parentAddr].RSSI && addr < mac.addr)
        {
            printf("### Changing parent prev: %02d (%d) new: %02d (%d)\n", prevParentAddr, neighbours.nodes[prevParentAddr].RSSI, addr, RSSI);
            parentAddr = addr;
            changed = true;
        }
        if (changed)
        {
            sem_wait(&neighbours.mutex);
            neighbours.nodes[prevParentAddr].role = NODE;
            neighbours.nodes[addr].role = PARENT;
            sem_post(&neighbours.mutex);
            printf("%s - Parent: %02d (%02d)\n", timestamp(), addr, RSSI);
            sendBeacon();
        }
    }
}

static void selectClosestNeighbour()
{
    uint8_t newParent = ADDR_SINK;
    uint8_t numActive = neighbours.numActive;
    int newParentRSSI = MIN_RSSI;

    sem_wait(&neighbours.mutex);
    for (int i = neighbours.minAddr, active = 0; i <= neighbours.maxAddr && active < numActive; i++)
    {
        NodeInfo node = neighbours.nodes[i];
        if (node.isActive)
        {
            if (node.role != CHILD && node.RSSI > newParentRSSI)
            {
                if (loglevel >= DEBUG)
                {
                    printf("%s - ##  Active: %02d (%02d)\n", timestamp(), node.addr, node.RSSI);
                }
                newParent = node.addr;
                newParentRSSI = node.RSSI;
            }
            active++;
        }
    }
    sem_post(&neighbours.mutex);
    parentAddr = newParent;
}

void selectClosestLowerNeighbour()
{
    uint8_t newParent = ADDR_SINK;
    uint8_t numActive = neighbours.numActive;
    int newParentRSSI = MIN_RSSI;

    sem_wait(&neighbours.mutex);
    for (int i = neighbours.minAddr, active = 0; i <= neighbours.maxAddr && active < numActive; i++)
    {
        NodeInfo node = neighbours.nodes[i];
        if (node.isActive)
        {
            if (node.role != CHILD && node.RSSI >= newParentRSSI && node.addr < mac.addr)
            {
                if (loglevel >= DEBUG)
                {
                    printf("%s - ##  Active: %02d (%02d)\n", timestamp(), node.addr, node.RSSI);
                }
                newParent = node.addr;
                newParentRSSI = node.RSSI;
            }
            active++;
        }
    }
    sem_post(&neighbours.mutex);
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

    sem_wait(&neighbours.mutex);
    for (int i = 0; i < mac.addr; i++)
    {
        NodeInfo node = neighbours.nodes[i];
        if (node.isActive)
        {
            if (loglevel >= DEBUG)
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
    sem_post(&neighbours.mutex);

    parentAddr = newParent;
}

static void selectRandomNeighbour()
{
    uint8_t newParent = ADDR_SINK;
    uint8_t numActive = neighbours.numActive;
    NodeInfo pool[numActive];
    int newParentRSSI = MIN_RSSI;
    int p = 0;

    sem_wait(&neighbours.mutex);
    for (int i = neighbours.minAddr, active = 0; i <= neighbours.maxAddr && active < numActive; i++)
    {
        NodeInfo node = neighbours.nodes[i];
        if (node.isActive)
        {
            if (node.addr != ADDR_SINK && node.role != CHILD && node.addr != parentAddr)
            {
                if (loglevel >= DEBUG)
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
    sem_post(&neighbours.mutex);
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
    uint8_t numActive = neighbours.numActive;
    NodeInfo pool[numActive];
    int newParentRSSI = MIN_RSSI;
    int p = 0;

    sem_wait(&neighbours.mutex);
    for (int i = 0; i < mac.addr; i++)
    {
        NodeInfo node = neighbours.nodes[i];
        if (node.isActive)
        {
            if (node.addr != ADDR_SINK && node.role != CHILD && node.addr < parentAddr)
            {
                if (loglevel >= DEBUG)
                {
                    printf("%s - ##  Active: %02d (%02d)\n", timestamp(), node.addr, node.RSSI);
                }
                pool[p].addr = node.addr;
                pool[p].RSSI = node.RSSI;
                p++;
            }
        }
    }
    sem_post(&neighbours.mutex);

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
    sem_wait(&neighbours.mutex);
    neighbours.nodes[prevParentAddr].role = NODE;
    neighbours.nodes[parentAddr].role = PARENT;
    sem_post(&neighbours.mutex);
    printf("%s - New parent: %02d (%02d)\n", timestamp(), parentAddr, neighbours.nodes[parentAddr].RSSI);
}

void initActiveNodes()
{
    sem_init(&neighbours.mutex, 0, 1);
    neighbours.numActive = 0;
    memset(neighbours.nodes, 0, sizeof(neighbours.nodes));
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
    for (int i = neighbours.minAddr, active = 0; i < neighbours.maxAddr && active < numActive; i++)
    {
        NodeInfo node = neighbours.nodes[i];
        if (node.isActive && (currentTime - node.lastSeen) > NODE_TIMEOUT)
        {
            NodeInfo *ptr = &node;
            ptr->isActive = false;
            ptr->role = NODE;
            neighbours.numActive--;
            active++;
            if (parentAddr == node.addr)
            {
                parentInactive = true;
            }
            printf("%s - Inactive: %02d\n", timestamp(), node.addr);
        }
    }
    numActive = neighbours.numActive;
    sem_post(&neighbours.mutex);
    if (parentInactive)
    {
        printf("%s - ##  Parent inactive: %02d\n", timestamp(), parentAddr);
        changeParent();
    }
    else
    {
        if (loglevel >= DEBUG)
        {
            printf("%s - ##  Active neighbour count: %d\n", timestamp(), numActive);
        }
    }
    neighbours.lastCleanupTime = time(NULL);
}

static void sendBeacon()
{
    Beacon beacon;
    beacon.ctrl = CTRL_BCN;
    beacon.parent = parentAddr;
    beacon.parentRSSI = neighbours.nodes[parentAddr].RSSI;
    if (!MAC_send(&mac, ADDR_BROADCAST, (uint8_t *)&beacon, sizeof(Beacon)))
    {
        printf("%s - ## Error: MAC_send failed %s:%d\n", timestamp(), __FILE__, __LINE__);
    }
}

static void *sendBeaconPeriodic(void *args)
{
    while (1)
    {
        printf("%s - Sending beacon\n", timestamp());
        sendBeacon();
        time_t current = time(NULL);
        if (current - neighbours.lastCleanupTime > NODE_TIMEOUT)
        {
            cleanupInactiveNodes();
        }
        sleep(beaconInterval);
    }
    return NULL;
}

static void *sendRoutingTable(void *args)
{
    sleep(routingTableInterval);
    while (1)
    {
        if (neighbours.numActive > 0)
        {
            RoutingMessage msg = buildRoutingTableMsg();
            if (msg.data != NULL)
            {
                printf("%s - Sending routing table\n", timestamp());
                sendQ_enqueue(msg);
            }
        }
        else
        {
            if (loglevel >= DEBUG)
            {
                printf("### No active neightbours\n");
            }
        }
        sleep(routingTableInterval);
    }
    return NULL;
}

static RoutingMessage buildRoutingTableMsg()
{
    // msg data format : [ numActive | ( addr,active,role,parent,rssi )*numActive ]
    RoutingMessage msg;
    msg.ctrl = CTRL_TAB;
    msg.src = mac.addr;
    msg.dest = ADDR_SINK;
    int numActive = neighbours.numActive;
    msg.len = 1 + (numActive * 12);
    msg.data = (uint8_t *)malloc(msg.len);
    if (msg.data == NULL)
    {
        printf("## - Error: malloc\n");
    }
    sem_wait(&neighbours.mutex);
    int offset = 0;
    msg.data[offset++] = numActive;
    if (loglevel >= DEBUG)
    {
        printf("Address,\tActive,\tRole,\tRSSI,\tParent,\tParentRSSI\n");
    }
    for (int i = neighbours.minAddr, active = 0; i <= neighbours.maxAddr && active < neighbours.numActive; i++)
    {
        NodeInfo node = neighbours.nodes[i];
        if (node.isActive)
        {
            msg.data[offset++] = node.addr;
            msg.data[offset++] = (uint8_t)node.isActive;
            msg.data[offset++] = (uint8_t)node.role;
            msg.data[offset++] = node.parent;
            int rssi = node.RSSI;
            memcpy(&msg.data[offset], &rssi, sizeof(rssi));
            offset += sizeof(rssi);
            int parentRSSI = node.parentRSSI;
            memcpy(&msg.data[offset], &parentRSSI, sizeof(parentRSSI));
            offset += sizeof(parentRSSI);
            active++;
            if (loglevel >= DEBUG)
            {
                printf("%02d,\t%d,\t%s,\t%d,\t%02d,\t%d\n", node.addr, node.isActive, getRoleStr(node.role), rssi, node.parent, parentRSSI);
            }
        }
    }
    sem_post(&neighbours.mutex);
    fflush(stdout);
    return msg;
}

static void parseRoutingTablePkt(RoutingMessage tab)
{
    NodeRoutingTable table;
    int dataLen = tab.len;
    uint8_t *data = tab.data;
    if (data == NULL || dataLen < 1)
    {
        printf("Invalid routing table data\n");
        return;
    }

    int offset = 0;
    int numActive = data[offset++];
    table.numActive = numActive;
    table.timestamp = timestamp();
    table.src = tab.src;
    printf("%s - Routing table of Node :%02d nodes:%d\n", timestamp(), tab.src, numActive);
    if (loglevel >= DEBUG)
    {
        printf("Source,\tAddress,\tActive,\tRole,\tRSSI,\tParent,\tParentRSSI\n");
    }

    for (int i = 0; i < numActive && offset < dataLen; i++)
    {
        uint8_t addr = data[offset++];
        table.nodes[i].addr = addr;
        bool active = (bool)data[offset++];
        table.nodes[i].isActive = active;
        NodeRole role = (NodeRole)data[offset++];
        table.nodes[i].role = role;
        uint8_t parent = data[offset++];
        table.nodes[i].parent = parent;

        int rssi;
        memcpy(&rssi, &data[offset], sizeof(int));
        offset += sizeof(int);
        table.nodes[i].RSSI = rssi;
        int parentRSSI;
        memcpy(&parentRSSI, &data[offset], sizeof(int));
        offset += sizeof(int);
        table.nodes[i].parentRSSI = parentRSSI;

        const char *roleStr = getRoleStr(role);
        if (loglevel >= DEBUG)
        {
            printf("%02d,\t%02d,\t%d,\t%s,\t%d,\t%02d,\t%d\n", tab.src, addr, active, roleStr, rssi, parent, parentRSSI);
        }
    }
    fflush(stdout);
    routingTables_enqueue(table);
}

static char *getRoleStr(NodeRole role)
{
    char *roleStr;
    switch (role)
    {
    case PARENT:
        roleStr = "PARENT";
        break;
    case CHILD:
        roleStr = "CHILD";
        break;
    case NODE:
        roleStr = "NODE";
        break;
    default:
        roleStr = "UNKNOWN";
        break;
    }
    return roleStr;
}

static void routingTables_init()
{
    routingTables.begin = 0;
    routingTables.end = 0;
    sem_init(&routingTables.full, 0, 0);
    sem_init(&routingTables.free, 0, MAX_ACTIVE_NODES);
    sem_init(&routingTables.mutex, 0, 1);
}

static void routingTables_enqueue(NodeRoutingTable table)
{
    sem_wait(&routingTables.free);
    sem_wait(&routingTables.mutex);
    routingTables.table[routingTables.end] = table;
    routingTables.end = (routingTables.end + 1) % MAX_ACTIVE_NODES;
    sem_post(&routingTables.mutex);
    sem_post(&routingTables.full);
}

static NodeRoutingTable routingTables_dequeue()
{
    sem_wait(&routingTables.full);
    sem_wait(&routingTables.mutex);
    NodeRoutingTable table = routingTables.table[routingTables.begin];
    routingTables.begin = (routingTables.begin + 1) % MAX_ACTIVE_NODES;
    sem_post(&routingTables.mutex);
    sem_post(&routingTables.free);
    return table;
}

static int routingTables_timed_dequeue(NodeRoutingTable *tab, struct timespec *ts)
{
    if (sem_timedwait(&routingTables.full, ts) == -1)
    {
        if (errno == ETIMEDOUT)
        {
            return 0;
        }
        return -1;
    }

    sem_wait(&routingTables.mutex);
    *tab = routingTables.table[routingTables.begin];
    routingTables.begin = (routingTables.begin + 1) % MAX_ACTIVE_NODES;
    sem_post(&routingTables.mutex);
    sem_post(&routingTables.free);
    return 1;
}

static void *saveRoutingTable(void *args)
{
    if (loglevel == TRACE)
    {
        // printf("Inside saveRoutingTable\n");
    }
    time_t start = time(NULL);
    while (1)
    {
        if (loglevel == TRACE)
        {
            // printf("saveRoutingTable : loop\n");
        }
        NodeRoutingTable table;
        // table = routingTables_dequeue();
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;

        int result = routingTables_timed_dequeue(&table, &ts);
        if (result <= 0)
        {
            printf("routingTables_timed_dequeue returned: %d\n", result);
            continue;
        }
        writeToCSVFile(table);
        time_t current = time(NULL);
        if (current - start > NODE_TIMEOUT)
        {
            printf("### Generating graph...\n");
            system("python /home/pi/sw_workspace/AlohaRoute/logs/script.py");
            start = current;
        }
        // usleep(5000000);
        sleep(2);
    }
}

static void writeToCSVFile(NodeRoutingTable table)
{
    FILE *file = fopen(outputCSV, "a");
    if (file == NULL)
    {
        printf("## - Error opening csv file!\n");
        exit(EXIT_FAILURE);
    }
    if (loglevel >= DEBUG)
    {
        printf("### Writing to CSV\n");
        printf("Timestamp, Source, Address, Active, Role, RSSI, Parent, ParentRSSI\n");
    }
    for (int i = 0; i < table.numActive; i++)
    {
        NodeInfo node = table.nodes[i];
        fprintf(file, "%s,%02d,%02d,%d,%s,%d,%02d,%d\n", table.timestamp, table.src, node.addr, node.isActive, getRoleStr(node.role), node.RSSI, node.parent, node.parentRSSI);
        if (loglevel >= DEBUG)
        {
            printf("%s, %02d, %02d, %d, %s, %d, %02d, %d\n", table.timestamp, table.src, node.addr, node.isActive, getRoleStr(node.role), node.RSSI, node.parent, node.parentRSSI);
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
    const char *header = "Timestamp,Source,Address,Active,Role,RSSI,Parent,ParentRSSI\n";
    fprintf(file, "%s", header);
    fclose(file);
    if (loglevel >= DEBUG)
    {
        printf("### pid:%d ppid:%d\n", getpid(), getppid());
        printf("### CSV file: %s created\n", outputCSV);
    }
}

static void installDependencies()
{
    int pid = fork();    
    if (pid == 0)
    {
        if (loglevel >= DEBUG)
        {
            printf("### Inside installDependencies, forked; local_pid:%d\n", pid);
            // printf("### installDependencies:  local_pid:%d pid:%d ppid:%d\n", pid, getpid(), getppid());
            printf("### Installing dependencies...\n");
            fflush(stdout);
        }
        // ###
        // if (loglevel == INFO)
        // {
        //     freopen("/dev/null", "w", stdout);
        // }
        
        char *cmd = "bash -c \"python -m pip install -r /home/pi/sw_workspace/AlohaRoute/logs/requirements.txt > /dev/null\"";
        // if (execl("/bin/sh", "sh", "-c", cmd, NULL) != 0)
        // if (execl("/usr/bin/pip", "pip", "install", "-r", "/home/pi/sw_workspace/AlohaRoute/logs/requirements.txt", NULL) != 0)
        int result = system(cmd);
        printf("### Result of %s : %d\n",*cmd,result);
        fflush(stdout);
        if (result != 0)
        {
            fprintf(stderr, "## Error installing dependencies. Exiting...\n");
            fclose(stdout);
            fclose(stderr);
            exit(EXIT_FAILURE);
        }
        fclose(stdout);
        fclose(stderr);
        exit(EXIT_SUCCESS);
    }
    else if (pid > 0)
    {
        wait(NULL);
        fflush(stdout);
        if (loglevel >= DEBUG)
        {
            // printf("### local_pid:%d pid:%d ppid:%d\n", pid, getpid(), getppid());
            printf("### Installed dependencies...\n");
            fflush(stdout);
        }
    }
    else
    {
        printf("## Error installing dependencies\n");
        exit(EXIT_FAILURE);
    }
}

static void createHttpServer()
{
    serverPid = fork();
    if (serverPid == 0)
    {
        // printf("### createHttpServer : local_serverPid:%d pid:%d ppid:%d\n", serverPid, getpid(), getppid());
        // freopen("/dev/null", "w", stdout);
        chdir("/home/pi/sw_workspace/AlohaRoute/Debug/results");
        // Kill the http server if running
        char *cleanup = "fuser 8000/tcp >/dev/null && kill -9 $(fuser 8000/tcp) || true";
        if(system(cleanup)!=0) {
            printf("## Error cleaning up the HTTP server\n");
            exit(EXIT_FAILURE);
        } else {
            printf("## Successfully cleaned up the HTTP server\n");
        }
        char *cmd = "python3 -m http.server 8000 --bind 0.0.0.0 &";
        // if (execl("/usr/bin/python3", "python3", "-m", "http.server", "8000", "--bind", "0.0.0.0", NULL) != 0)
        if (system(cmd) != 0)
        {
            printf("## Error starting HTTP server\n");
            fclose(stdout);
            fclose(stderr);
            exit(EXIT_FAILURE);
        }
        fclose(stdout);
        fclose(stderr);
        exit(EXIT_SUCCESS);
    }
    else if (serverPid > 0)
    {
        wait(NULL);
        if (loglevel >= DEBUG)
        {
            // printf("### local_serverPid:%d pid:%d ppid:%d\n", serverPid, getpid(), getppid());
            printf("HTTP server started on port: %d with PID: %d\n", 8000, serverPid);
        }
        else
        {
            // printf("### local_serverPid:%d pid:%d ppid:%d\n", serverPid, getpid(), getppid());
            printf("HTTP server started on port: %d\n", 8000);
        }
        signal(SIGSEGV, signalHandler);
        signal(SIGABRT, signalHandler);
        signal(SIGFPE, signalHandler);
        signal(SIGILL, signalHandler);
        signal(SIGTERM, signalHandler);
    }
    else
    {
        printf("## Error creating HTTP server\n");
        exit(EXIT_FAILURE);
    }
}

static void signalHandler(int signum)
{

    printf("### local_serverPid:%d pid:%d ppid:%d\n", serverPid, getpid(), getppid());

    if (loglevel >= DEBUG)
    {
        printf("### Received signal :%d\n", signum);
    }
    // if (serverPid > 0)
    {
        if (loglevel >= DEBUG)
        {
            printf("### local_serverPid:%d pid:%d ppid:%d\n", serverPid, getpid(), getppid());
            printf("### Stopping HTTP server pid:%d\n", serverPid);
        }
        kill(serverPid, SIGTERM);
        int status;
        waitpid(serverPid, &status, 0);
        printf("HTTP server stopped.\n");
    }
    exit(EXIT_SUCCESS);
}