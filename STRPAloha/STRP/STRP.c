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
#include "../ALOHA/ALOHA.h"
#include "../GPIO/GPIO.h"
#include "../common.h"
#include "../util.h"

#define PACKETQ_SIZE 32
#define MIN_RSSI -128

// Packet control flags
#define CTRL_PKT '\x45' // STRP packet
#define CTRL_BCN '\x47' // STRP beacon
#define CTRL_TAB '\x48' // STRP routing table packet

typedef struct Beacon
{
    uint8_t ctrl;
    uint8_t parent;
    int parentRSSI;
} Beacon;

typedef struct DataPacket
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

} DataPacket;

typedef struct PacketQueue
{
    unsigned int begin, end;
    struct DataPacket packet[PACKETQ_SIZE];
    sem_t mutex, full, free;
} PacketQueue;

typedef struct TableQueue
{
    NodeRoutingTable table[MAX_ACTIVE_NODES];
    sem_t mutex, full, free;
    unsigned int begin, end;
} TableQueue;

static PacketQueue sendQ, recvQ;
static pthread_t recvT;
static pthread_t sendT;
static MAC mac;
static const unsigned short headerSize = 8; // [ ctrl | dest | src | parent | numHops[2] | len[2] | [data[len] ]
static uint8_t parentAddr;
static ActiveNodes neighbours;
static uint8_t loopyParent;
static STRP_Config config;
static TableQueue tableQ;

int (*Routing_sendMsg)(uint8_t dest, uint8_t *data, unsigned int len) = STRP_sendMsg;                    // ####
int (*Routing_recvMsg)(Routing_Header *h, uint8_t *data) = STRP_recvMsg;                                 // ####
int (*Routing_timedRecvMsg)(Routing_Header *h, uint8_t *data, unsigned int timeout) = STRP_timedRecvMsg; // ####

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
static void sendBeacon();
static void *sendBeaconPeriodic(void *args);
static DataPacket buildRoutingTablePkt();
static void parseRoutingTablePkt(DataPacket tab);
static void tableQ_init();
static void tableQ_enqueue(NodeRoutingTable table);
static NodeRoutingTable tableQ_dequeue();
static uint8_t tableQ_timed_dequeue(NodeRoutingTable *tab, struct timespec *ts);
char *getNodeStateStr(const NodeState state);
char *getNodeRoleStr(const NodeRole role);
static char *getRoutingStrategyStr();
static NodeRoutingTable ActiveNodesToNRT(const ActiveNodes activeNodes, uint8_t src);

// Initialize the STRP
int STRP_init(STRP_Config c)
{
    pthread_t sendBeaconT;
    config = c;
    srand(config.self * time(NULL));
    ALOHA_init(&mac, config.self);
    mac.debug = 0;
    mac.recvTimeout = config.recvTimeoutMs;
    sendQ_init();
    recvQ_init();
    initActiveNodes();
    if (pthread_create(&recvT, NULL, recvPackets_func, &mac) != 0)
    {
        printf("### Error: Failed to create Routing receive thread");
        exit(EXIT_FAILURE);
    }
    if (config.self != ADDR_SINK)
    {
        printf("%s - Routing Strategy:  %s\n", timestamp(), getRoutingStrategyStr());
        selectParent();
    }
    else
    {
        tableQ_init();
        sendBeacon();
    }

    if (config.self != ADDR_SINK)
    {
        if (pthread_create(&sendT, NULL, sendPackets_func, &mac) != 0)
        {
            printf("### Error: Failed to create Routing send thread");
            exit(EXIT_FAILURE);
        }
    }
    // Beacon thread
    if (pthread_create(&sendBeaconT, NULL, sendBeaconPeriodic, &mac) != 0)
    {
        printf("### Error: Failed to create sendBeaconPeriodic thread");
        exit(EXIT_FAILURE);
    }
    return 1;
}

// Send a message via STRP
int STRP_sendMsg(uint8_t dest, uint8_t *data, unsigned int len)
{
    DataPacket msg;
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
        printf("# %s - Error: msg.data is NULL %s:%d\n", timestamp(), __FILE__, __LINE__);
    }
    memcpy(msg.data, data, len);
    sendQ_enqueue(msg);
    return 1;
}

// Receive a message via STRP
int STRP_recvMsg(Routing_Header *header, uint8_t *data)
{
    DataPacket msg = recvMsgQ_dequeue();
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
        printf("# %s - Error: msg.data is NULL %s:%d\n", timestamp(), __FILE__, __LINE__);
    }

    return msg.len;
}

// Exclusively for NODE: Send the routing table to the sink
// Returns 0 if sending skipped as routing table has no active neighbors.
// Returns 1 otherwise.
int STRP_sendRoutingTable()
{
    if (neighbours.numActive > 0)
    {
        DataPacket msg = buildRoutingTablePkt();
        if (msg.data != NULL)
        {
            printf("%s - Sending routing table\n", timestamp());
            sendQ_enqueue(msg);
        }
    }
    return neighbours.numActive > 0;
}

// Exclusively for SINK: Receive a routing table sent by a NODE. Blocks the thread till the timeout.
// Returns 1 for suceess, 0 for timeout
int STRP_timedRecvRoutingTable(Routing_Header *header, NodeRoutingTable *table, unsigned int timeout)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout;

    int result = tableQ_timed_dequeue(table, &ts);
    if (result == 1)
    {
        header->RSSI = neighbours.nodes[table->src].RSSI;
        header->src = table->src;
    }
    return result == 1;
}

// Exclusively for SINK: Receive a routing table sent by a NODE. Blocks the thread indefinitely.
NodeRoutingTable STRP_recvRoutingTable(Routing_Header *header)
{
    NodeRoutingTable table = tableQ_dequeue();

    header->RSSI = neighbours.nodes[table.src].RSSI;
    header->src = table.src;
    return table;
}

// Receive a message via STRP. Blocks the thread till the timeout.
// Returns 0 for timeout, -1 for error
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
        printf("# %s - Error: msg.data is NULL %s:%d\n", timestamp(), __FILE__, __LINE__);
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
        if (pkt == NULL)
        {
            // free(pkt);
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
        if (ctrl == CTRL_PKT || ctrl == CTRL_TAB)
        {
            DataPacket msg = deserializePacket(pkt);
            updateActiveNodes(mac.recvH.src_addr, mac.RSSI, ADDR_BROADCAST, MIN_RSSI);
            // if (msg.dest == ADDR_BROADCAST || msg.dest == macTemp->addr)
            if (msg.dest == macTemp->addr)
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
                        if (config.loglevel >= DEBUG)
                        {
                            printf("# %s - Skipping parent change... loopyParent:%02d\n", timestamp(), loopyParent);
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
                    printf("# %s - Error FWD: %02d (%02d) -> %02d\n", timestamp(), msg.src, msg.numHops, parentAddr);
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
            if (config.loglevel >= DEBUG)
            {
                printf("# %s - Beacon src: %02d (%d) parent: %02d(%d)\n", timestamp(), mac.recvH.src_addr, mac.RSSI, beacon->parent, beacon->parentRSSI);
            }
            updateActiveNodes(mac.recvH.src_addr, mac.RSSI, beacon->parent, beacon->parentRSSI);
        }
        else
        {
            if (config.loglevel >= DEBUG)
            {
                printf("# %s - STRP : Unknown control flag %02d \n", timestamp(), ctrl);
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
static DataPacket deserializePacket(uint8_t *pkt)
{
    DataPacket msg;
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
        DataPacket msg = sendQ_dequeue();
        uint8_t *pkt;
        unsigned int pktSize = serializePacket(msg, &pkt);
        if (pktSize < 0)
        {
            // free(pkt);
            continue;
        }
        if (!MAC_send(macTemp, parentAddr, pkt, pktSize))
        {
            printf("%s - ### Error: MAC_send failed %s:%d\n", timestamp(), __FILE__, __LINE__);
        }
        else
        {
        }
        free(pkt);
        usleep(1000);
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

static void *sendBeaconHandler(void *args)
{

    MAC *m = (MAC *)args;
    int trials = 0;
    if (config.loglevel >= DEBUG)
    {
        printf("# %s - Sending beacons...\n", timestamp());
        fflush(stdout);
    }

    time_t start = time(NULL);
    time_t current;
    do
    {
        sendBeacon();
        trials++;
        usleep(randInRange(500000, 1200000));
        current = time(NULL);
    } while (current - start < config.senseDurationS);

    if (config.loglevel >= DEBUG)
    {
        printf("# %s - Sent %d beacons...\n", timestamp(), trials);
        fflush(stdout);
    }
    return NULL;
}

static void *receiveBeaconHandler(void *args)
{
    MAC *m = (MAC *)args;
    int trials = 0;
    if (config.loglevel >= DEBUG)
    {
        printf("# %s - Listening for beacons...\n", timestamp());
    }
    time_t start = time(NULL);
    time_t current;
    do
    {
        unsigned char data[sizeof(Beacon)];
        int len = MAC_timedRecv(m, (unsigned char *)&data, 1);
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
    } while (current - start <= config.senseDurationS);
    if (config.loglevel >= DEBUG)
    {
        printf("# %s - Received %d beacons...\n", timestamp(), trials);
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
    // sleep(config.senseDurationS + 1);
    // pthread_join(recv, NULL);
    pthread_join(send, NULL);
    sleep(5);
    printf("%s - Parent: %02d (%02d)\n", timestamp(), parentAddr, neighbours.nodes[parentAddr].RSSI);
}

static void updateActiveNodes(uint8_t addr, int RSSI, uint8_t parent, int parentRSSI)
{
    if (config.loglevel == TRACE)
    {
        printf("## %s - Inside updateActiveNodes addr:%02d (%d) parent:%02d(%d)\n", timestamp(), addr, RSSI, parent, parentRSSI);
    }
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
        nodePtr->role = PARENT;
    }
    else if (parent == mac.addr)
    {
        nodePtr->role = CHILD;
        child = true;
    }
    else
    {
        nodePtr->role = NODE;
    }
    if (parent != ADDR_BROADCAST)
    {
        nodePtr->parent = parent;
        nodePtr->parentRSSI = parentRSSI;
    }
    nodePtr->RSSI = RSSI;
    nodePtr->lastSeen = time(NULL);
    sem_post(&neighbours.mutex);
    if (child && parentAddr == addr && addr < mac.addr)
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
    }
    // change parent if new neighbour fits
    if (mac.addr != ADDR_SINK && !child && addr != parentAddr)
    {
        if (config.loglevel == TRACE)
        {
            printf("## %s - Inside change block parent:%02d (%d)\n", timestamp(), parentAddr, neighbours.nodes[parentAddr].RSSI);
        }
        bool changed = false;
        uint8_t prevParentAddr = parentAddr;
        if (config.strategy == NEXT_LOWER && addr > parentAddr && addr < mac.addr)
        {
            parentAddr = addr;
            changed = true;
        }
        if (config.strategy == RANDOM && (rand() % 101) < 50)
        {
            parentAddr = addr;
            changed = true;
        }
        if (config.strategy == RANDOM_LOWER && addr < mac.addr && (rand() % 101) < 50)
        {
            parentAddr = addr;
            changed = true;
        }
        if (config.strategy == CLOSEST && RSSI > neighbours.nodes[parentAddr].RSSI)
        {
            parentAddr = addr;
            changed = true;
        }
        if (config.strategy == CLOSEST_LOWER && RSSI > neighbours.nodes[parentAddr].RSSI && addr < mac.addr)
        {
            printf("# %s - Changing parent. Prev: %02d (%d) New: %02d (%d)\n", timestamp(), prevParentAddr, neighbours.nodes[prevParentAddr].RSSI, addr, RSSI);
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
    for (uint8_t i = neighbours.minAddr, active = 0; i <= neighbours.maxAddr && active < numActive; i++)
    {
        NodeInfo node = neighbours.nodes[i];
        if (node.state == ACTIVE)
        {
            if (node.role != CHILD && node.RSSI > newParentRSSI)
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
    sem_post(&neighbours.mutex);
    parentAddr = newParent;
}

void selectClosestLowerNeighbour()
{
    uint8_t newParent = ADDR_SINK;
    uint8_t numActive = neighbours.numActive;
    int newParentRSSI = MIN_RSSI;

    sem_wait(&neighbours.mutex);
    for (uint8_t i = neighbours.minAddr, active = 0; i <= neighbours.maxAddr && active < numActive; i++)
    {
        NodeInfo node = neighbours.nodes[i];
        if (node.state == ACTIVE)
        {
            if (node.role != CHILD && node.RSSI >= newParentRSSI && node.addr < mac.addr)
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
    default:
        strategyStr = "UNKNOWN";
        break;
    }
    // printf("%s - Routing strategy: %s\n", timestamp(), strategyStr);
    return strategyStr;
}

static void selectNextLowerNeighbour()
{
    uint8_t newParent = ADDR_SINK;
    int newParentRSSI = MIN_RSSI;

    sem_wait(&neighbours.mutex);
    for (uint8_t i = 0; i < mac.addr; i++)
    {
        NodeInfo node = neighbours.nodes[i];
        if (node.state == ACTIVE)
        {
            if (config.loglevel >= DEBUG)
            {
                printf("# %s - Active: %02d (%02d)\n", timestamp(), node.addr, node.RSSI);
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
    uint8_t p = 0;

    sem_wait(&neighbours.mutex);
    for (uint8_t i = neighbours.minAddr, active = 0; i <= neighbours.maxAddr && active < numActive; i++)
    {
        NodeInfo node = neighbours.nodes[i];
        if (node.state == ACTIVE)
        {
            if (node.addr != ADDR_SINK && node.role != CHILD && node.addr != parentAddr)
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
    uint8_t p = 0;

    sem_wait(&neighbours.mutex);
    for (uint8_t i = 0; i < mac.addr; i++)
    {
        NodeInfo node = neighbours.nodes[i];
        if (node.state == ACTIVE)
        {
            if (node.addr != ADDR_SINK && node.role != CHILD && node.addr < parentAddr)
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
            nodePtr->role = NODE;
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
    if (!MAC_send(&mac, ADDR_BROADCAST, (uint8_t *)&beacon, sizeof(Beacon)))
    {
        printf("%s - ### Error: MAC_send failed %s:%d\n", timestamp(), __FILE__, __LINE__);
    }
}

static void *sendBeaconPeriodic(void *args)
{
    while (1)
    {
        sendBeacon();
        time_t current = time(NULL);
        if ((current - neighbours.lastCleanupTime) > config.nodeTimeoutS)
        {
            cleanupInactiveNodes();
        }
        sleep(config.beaconIntervalS);
    }
    return NULL;
}

static DataPacket buildRoutingTablePkt()
{
    // msg data format : [ numActive | ( addr,state,role,rssi,parent,parentRSSI,lastSeen )*numActive ]
    DataPacket msg;
    msg.ctrl = CTRL_TAB;
    msg.src = mac.addr;
    msg.dest = ADDR_SINK;
    const ActiveNodes table = neighbours;
    uint8_t numNodes = table.numNodes;
    msg.len = 1 + (numNodes * 20);
    msg.data = (uint8_t *)malloc(msg.len);
    if (msg.data == NULL)
    {
        printf("### - Error: malloc\n");
    }
    int offset = 0;
    msg.data[offset++] = numNodes;
    if (config.loglevel >= DEBUG)
    {
        printf("# Address,\tState,\tRole,\tRSSI,\tParent,\tParentRSSI\n");
    }
    for (uint8_t i = table.minAddr; i <= table.maxAddr; i++)
    {
        NodeInfo node = table.nodes[i];
        if (node.state != UNKNOWN)
        {
            msg.data[offset++] = node.addr;
            msg.data[offset++] = (uint8_t)node.state;
            msg.data[offset++] = (uint8_t)node.role;
            msg.data[offset++] = node.parent;
            int rssi = node.RSSI;
            memcpy(&msg.data[offset], &rssi, sizeof(rssi));
            offset += sizeof(rssi);
            int parentRSSI = node.parentRSSI;
            memcpy(&msg.data[offset], &parentRSSI, sizeof(parentRSSI));
            offset += sizeof(parentRSSI);
            time_t lastSeen = node.lastSeen;
            memcpy(&msg.data[offset], &lastSeen, sizeof(lastSeen));
            offset += sizeof(lastSeen);
            if (config.loglevel >= DEBUG)
            {
                printf("# %02d,\t%s,\t%s,\t%d,\t%02d,\t%d\n", node.addr, getNodeStateStr(node.state), getNodeRoleStr(node.role), rssi, node.parent, parentRSSI);
            }
        }
    }
    fflush(stdout);
    return msg;
}

static void parseRoutingTablePkt(DataPacket tab)
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
    uint8_t numNodes = data[offset++];
    table.numNodes = numNodes;
    table.timestamp = timestamp();
    table.src = tab.src;
    printf("%s - Routing table of Node :%02d nodes:%d\n", timestamp(), tab.src, numNodes);
    if (config.loglevel >= DEBUG)
    {
        printf("# Source,\tAddress,\tActive,\tRole,\tRSSI,\tParent,\tParentRSSI\n");
    }

    for (uint8_t i = 0; i < numNodes && offset < dataLen; i++)
    {
        uint8_t addr = data[offset++];
        table.nodes[i].addr = addr;
        NodeState state = (NodeState)data[offset++];
        table.nodes[i].state = state;
        NodeRole role = (NodeRole)data[offset++];
        table.nodes[i].role = role;
        uint8_t parent = data[offset++];
        table.nodes[i].parent = parent;

        int rssi;
        memcpy(&rssi, &data[offset], sizeof(rssi));
        offset += sizeof(rssi);
        table.nodes[i].RSSI = rssi;
        int parentRSSI;
        memcpy(&parentRSSI, &data[offset], sizeof(parentRSSI));
        offset += sizeof(parentRSSI);
        table.nodes[i].parentRSSI = parentRSSI;
        time_t lastSeen;
        memcpy(&lastSeen, &data[offset], sizeof(lastSeen));
        offset += sizeof(lastSeen);
        table.nodes[i].lastSeen = lastSeen;
        const char *roleStr = getNodeRoleStr(role);
        const char *stateStr = getNodeStateStr(state);
        if (config.loglevel >= DEBUG)
        {
            printf("# %02d,\t%02d,\t%s,\t%s,\t%d,\t%02d,\t%d\n", tab.src, addr, stateStr, roleStr, rssi, parent, parentRSSI);
        }
    }
    fflush(stdout);
    tableQ_enqueue(table);
}

char *getNodeRoleStr(const NodeRole role)
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
        roleStr = "ERROR";
        break;
    }
    return roleStr;
}

NodeRoutingTable getSinkRoutingTable()
{
    NodeRoutingTable table;
    const ActiveNodes activeNodes = neighbours;
    uint8_t max = neighbours.maxAddr;
    uint8_t min = neighbours.minAddr;
    table.src = config.self;
    table.timestamp = timestamp();
    table.numNodes = activeNodes.numNodes;
    for (uint8_t addr = min, i = 0; addr <= max && i < activeNodes.numNodes; addr++)
    {
        if (activeNodes.nodes[addr].state != UNKNOWN)
        {
            table.nodes[i++] = activeNodes.nodes[addr];
        }
    }
    return table;
}

char *getNodeStateStr(const NodeState state)
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

static void tableQ_init()
{
    tableQ.begin = 0;
    tableQ.end = 0;
    sem_init(&tableQ.full, 0, 0);
    sem_init(&tableQ.free, 0, MAX_ACTIVE_NODES);
    sem_init(&tableQ.mutex, 0, 1);
}

static void tableQ_enqueue(NodeRoutingTable table)
{
    sem_wait(&tableQ.free);
    sem_wait(&tableQ.mutex);
    tableQ.table[tableQ.end] = table;
    tableQ.end = (tableQ.end + 1) % MAX_ACTIVE_NODES;
    sem_post(&tableQ.mutex);
    sem_post(&tableQ.full);
}

static NodeRoutingTable tableQ_dequeue()
{
    sem_wait(&tableQ.full);
    sem_wait(&tableQ.mutex);
    NodeRoutingTable table = tableQ.table[tableQ.begin];
    tableQ.begin = (tableQ.begin + 1) % MAX_ACTIVE_NODES;
    sem_post(&tableQ.mutex);
    sem_post(&tableQ.free);
    return table;
}

static uint8_t tableQ_timed_dequeue(NodeRoutingTable *tab, struct timespec *ts)
{
    if (sem_timedwait(&tableQ.full, ts) == -1)
    {
        if (errno == ETIMEDOUT)
        {
            return 0;
        }
        return -1;
    }

    sem_wait(&tableQ.mutex);
    *tab = tableQ.table[tableQ.begin];
    tableQ.begin = (tableQ.begin + 1) % MAX_ACTIVE_NODES;
    sem_post(&tableQ.mutex);
    sem_post(&tableQ.free);
    return 1;
}

static NodeRoutingTable ActiveNodesToNRT(const ActiveNodes activeNodes, uint8_t src)
{
    NodeRoutingTable table;
    table.src = src;
    table.numNodes = activeNodes.numNodes;
    for (uint8_t i = 0; i < activeNodes.numNodes; i++)
    {
        table.nodes[i] = activeNodes.nodes[i];
    }
    table.timestamp = timestamp();
    return table;
}

// ####
uint8_t STRP_getNextHop(uint8_t dest)
{
    return parentAddr;
}

uint8_t STRP_getHeaderSize()
{
    return headerSize;
}
