#include <errno.h>     // errno
#include <pthread.h>   // pthread_create
#include <semaphore.h> // sem_init, sem_wait, sem_trywait, sem_timedwait
#include <stdbool.h>   // bool, true, false
#include <stdio.h>     // printf
#include <stdlib.h>    // rand, malloc, free, exit
#include <string.h>    // memcpy, strerror
#include <time.h>

#include "routing.h"
#include "../ALOHA/ALOHA.h"
#include "../GPIO/GPIO.h"
#include "../common.h"
#include "../util.h"

typedef struct RoutingMessage
{
    uint8_t src;
    uint8_t prev;
    uint16_t numHops;
    uint8_t dest;
    uint8_t next;
    uint8_t *data;
    uint16_t len;
    uint8_t ctrl;
} RoutingMessage;

#define RoutingQueueSize 64

typedef struct RoutingQueue
{
    unsigned int begin, end;
    struct RoutingMessage packet[RoutingQueueSize];
    sem_t mutex, full, free;
} RoutingQueue;

static RoutingQueue sendQ, recvQ;

static pthread_t recvT;
static pthread_t sendT;
static MAC mac;
static uint8_t debugFlag;
static const unsigned short maxTrials = 4;
static const unsigned short headerSize = 7;

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
static uint8_t getNextHopAddr(uint8_t self);
static MAC initMAC(uint8_t addr, unsigned short debug, unsigned int timeout);
static void setDebug(uint8_t d);
static void recvMsgQ_timed_dequeue(RoutingMessage *msg, struct timespec *ts);

// Initialize the routing layer
int routingInit(uint8_t self, uint8_t debug, unsigned int timeout)
{
    // printf("%s - ### Inside routingInit\n", timestamp());
    setDebug(debug);
    // mac = initMAC(self, debugFlag, timeout);
    // srand(self * time(NULL));

    MAC_init(&mac, self);
    mac.debug = debug;
    mac.recvTimeout = timeout;
    mac.noiseThreshold = -65;

    sendQ_init();
    recvQ_init();
    // if (self == ADDR_SINK)
    {
        if (pthread_create(&recvT, NULL, recvPackets_func, &mac) != 0)
        {
            printf("Failed to create Routing receive thread");
            exit(1);
        }
        // printf("%s - ### Routing : recvT created\n", timestamp());
    }

    if (self != ADDR_SINK)
    {
        if (pthread_create(&sendT, NULL, sendPackets_func, &mac) != 0)
        {
            printf("Failed to create Routing send thread");
            exit(1);
        }
        // printf("%s - ### Routing : sendT created\n", timestamp());
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
    msg.next = getNextHopAddr(mac.addr);
    msg.numHops = 0;

    // printf("%s - ### routingSend: malloc \n", timestamp());

    msg.data = (uint8_t *)malloc(len);
    if (msg.data != NULL)
    {
        memcpy(msg.data, data, len);
    }
    else
    {
        // Handle memory allocation failure
        // printf("%s - ### routingSend: memory allocation failure\n", timestamp());
    }
    memcpy(msg.data, data, len);
    // printf("### - routingSend %02X %02X %02X %02d %02d %s|\n", msg.ctrl, msg.dest, msg.src, msg.numHops, msg.len, msg.data);
    sendQ_enqueue(msg);
    // free(msg.data);
    // printf("%s - ### routingSend: free \n", timestamp());

    // MAC_send(&mac, dest, data, len);

    return 1;
}

// Receive a message via the routing layer
int routingReceive(RouteHeader *header, uint8_t *data)
{
    // printf("%s - ### Inside routingReceive\n", timestamp());
    RoutingMessage msg = recvMsgQ_dequeue();
    header->dst = msg.dest;
    header->RSSI = mac.RSSI;
    header->src = msg.src;
    header->numHops = msg.numHops;
    if (msg.data != NULL)
    {
        memcpy(data, msg.data, msg.len);
    }
    else
    {
        // printf("%s - ### routingReceive: data is NULL\n", timestamp());
    }
    return msg.len;

    // MAC_recv(&mac, data);
}

// Receive a message via the routing layer
int routingTimedReceive(RouteHeader *header, uint8_t *data, unsigned int timeout)
{
    // printf("%s - ### Inside routingTimedReceive\n", timestamp());
    RoutingMessage msg;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout;
    recvMsgQ_timed_dequeue(&msg, &ts);
    header->dst = msg.dest;
    header->RSSI = mac.RSSI;
    header->src = msg.src;
    header->numHops = msg.numHops;
    if (msg.data != NULL)
    {
        // printf("%s - ### routingTimedReceive: %02X -> %02X\n", timestamp(), msg.src, msg.dest);
        memcpy(data, msg.data, msg.len);
    }
    else
    {
        // Handle null pointer case
        // printf("%s - ### routingTimedReceive: data is NULL\n", timestamp());
    }
    // *data = *msg.data;
    return msg.len;
}

static void recvMsgQ_timed_dequeue(RoutingMessage *msg, struct timespec *ts)
{
    // printf("%s - ### Inside recvMsgQ_timed_dequeue\n", timestamp());
    if (sem_timedwait(&recvQ.full, ts) == -1)
    {
        // printf("%s - ### recvMsgQ_timed_dequeue timeout\n", timestamp());
        return;
    }

    sem_wait(&recvQ.mutex);
    *msg = recvQ.packet[recvQ.begin];
    recvQ.begin = (recvQ.begin + 1) % RoutingQueueSize;
    sem_post(&recvQ.mutex);
    sem_post(&recvQ.full);
}

static void sendQ_init()
{
    // printf("%s - ### Inside sendQ_init\n", timestamp());

    sendQ.begin = 0;
    sendQ.end = 0;

    sem_init(&sendQ.full, 0, 0);
    sem_init(&sendQ.free, 0, RoutingQueueSize);
    sem_init(&sendQ.mutex, 0, 1);
}

static void recvQ_init()
{
    // printf("%s - ### Inside recvQ_init\n", timestamp());

    recvQ.begin = 0;
    recvQ.end = 0;

    sem_init(&recvQ.full, 0, 0);
    sem_init(&recvQ.free, 0, RoutingQueueSize);
    sem_init(&recvQ.mutex, 0, 1);
}

static void sendQ_enqueue(RoutingMessage msg)
{
    // printf("%s - ### Inside sendQ_enqueue\n", timestamp());

    sem_wait(&sendQ.free);
    sem_wait(&sendQ.mutex);

    sendQ.packet[sendQ.end] = msg;
    sendQ.end = (sendQ.end + 1) % RoutingQueueSize;

    sem_post(&sendQ.mutex);
    sem_post(&sendQ.full);
}

static RoutingMessage sendQ_dequeue()
{
    // printf("%s - ### Inside sendQ_dequeue\n", timestamp());

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
    // printf("%s - ### Inside recvQ_enqueue\n", timestamp());

    sem_wait(&recvQ.free);
    sem_wait(&recvQ.mutex);

    recvQ.packet[recvQ.end] = msg;
    recvQ.end = (recvQ.end + 1) % RoutingQueueSize;

    sem_post(&recvQ.mutex);
    sem_post(&recvQ.full);
}

static RoutingMessage recvMsgQ_dequeue()
{
    // printf("%s - ### Inside recvMsgQ_dequeue\n", timestamp());
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
    // printf("%s - ### Inside recvPackets_func XX\n", timestamp());

    MAC *macTemp = (MAC *)args;
    // printf("%s - ### Inside recvPackets_func addr = %02X\n", timestamp(), macTemp->addr);

    while (1)
    {
        uint8_t *pkt = (uint8_t *)malloc(240);
        // printf("%s - ### recvPackets_func: malloc %d\n", timestamp(), pkt);
        if (pkt == NULL)
        {
            // printf("%s - ### recvPackets_func: malloc failed\n", timestamp());
            free(pkt);
            continue;
        }
        // Receive a packet
        // int pktSize = MAC_recv(macTemp, pkt);
        int pktSize = MAC_timedrecv(macTemp, pkt, 3);

        if (pktSize == 0)
        {
            // printf("%s - ### recvPackets_func: pktSize == 0 free pkt\n", timestamp());
            free(pkt);
            continue;
        }

        // printf("%s - ### recvPackets_func: extract ctrl\n", timestamp());

        uint8_t ctrl = *pkt;
        // printf("%s - ### recvPackets_func: done extract ctrl\n", timestamp());

        if (ctrl == CTRL_PKT)
        {
            RoutingMessage msg = buildRoutingMessage(pkt);
            printf("%s - ### Received: (%02d) %02X -> %02X msg: %s\n", timestamp(), msg.numHops, msg.src, msg.dest, msg.data);

            if (msg.dest == ADDR_BROADCAST || msg.dest == macTemp->addr)
            {
                // Keep
                // --msg.numHops;
                recvQ_enqueue(msg);
            }
            else
            {
                ++msg.numHops;
                msg.next = getNextHopAddr(macTemp->addr);
                printf("%s - FWD: (%02d) %02X -> %02X msg: %s\n", timestamp(), msg.numHops, msg.src, msg.next, msg.data);
                // Forward
                if (MAC_send(macTemp, msg.next, pkt, pktSize))
                {
                    if (debugFlag)
                    {
                        printf("%s - FWD: (%02d) %02X -> %02X msg: %s\n", timestamp(), msg.numHops, msg.src, msg.next, msg.data);
                    }
                }
                else
                {
                    printf("%s - ## Error FWD: (%02d) %02X -> %02X msg: %s\n", timestamp(), msg.numHops, msg.src, msg.next, msg.data);
                }
                if (msg.data != NULL)
                {
                    free(msg.data);
                    // printf("%s - ### recvPackets_func: free(msg.data)\n", timestamp());
                }
            }
        }
        else
        {
            printf("%s - ## Routing : Unknown control flag %02X \n", timestamp(), ctrl);
        }

        free(pkt);
        // printf("%s - ### recvPackets_func: free(pkt)\n", timestamp());
    }
}

// Construct RoutingMessage
static RoutingMessage buildRoutingMessage(uint8_t *pkt)
{
    // printf("%s - ### Inside buildRoutingMessage\n", timestamp());

    RoutingMessage msg;
    // printf("%s - ### buildRoutingMessage pkt:%s\n", timestamp(), *pkt);

    msg.ctrl = *pkt;
    pkt += sizeof(msg.ctrl);
    // printf("%s - ### buildRoutingMessage ctrl : %02X\n", timestamp(), pkt[0]);

    msg.dest = *pkt;
    pkt += sizeof(msg.dest);
    // printf("%s - ### buildRoutingMessage dest : %02X\n", timestamp(), pkt[1]);

    msg.src = *pkt;
    pkt += sizeof(msg.src);
    // printf("%s - ### buildRoutingMessage src : %02X\n", timestamp(), pkt[2]);

    msg.prev = mac.recvH.src_addr;
    // printf("%s - ### buildRoutingMessage prev : %02X\n", timestamp(), msg.prev);

    // msg.numHops = (uint16_t)*pkt;
    // msg.numHops = (uint16_t)(*pkt << 8 | *(pkt + 1));
    memcpy(&msg.numHops, pkt, sizeof(msg.numHops));
    ++msg.numHops;
    pkt += sizeof(msg.numHops);
    // printf("%s - ### buildRoutingMessage numHops: %02X\n", timestamp(), msg.numHops);

    // msg.len = (uint16_t)*pkt;
    // msg.len = (uint16_t)(*pkt << 8 | *(pkt + 1));
    memcpy(&msg.len, pkt, sizeof(msg.len));
    pkt += sizeof(msg.len);
    // printf("%s - ### buildRoutingMessage len : %d\n", timestamp(), msg.len);

    if (msg.len > 0)
    {
        // printf("%s - ### buildRoutingMessage: malloc \n", timestamp());

        msg.data = (uint8_t *)malloc(msg.len);
    }
    else
    {
        msg.data = NULL;
    }
    memcpy(msg.data, pkt, msg.len);
    // printf("%s - ### buildRoutingMessage data : %s\n", timestamp(), msg.data);

    // free(msg.data);
    // printf("%s - ### buildRoutingMessage: free \n", timestamp());

    return msg;
}

static void *sendPackets_func(void *args)
{
    // printf("%s - ### Inside sendPackets_func\n", timestamp());

    MAC *macTemp = (MAC *)args;

    while (1)
    {
        // printf("%s - ### sendPackets_func addr: %02X\n", timestamp(), macTemp->addr);

        RoutingMessage msg = sendQ_dequeue();
        // printf("### - sendPackets_func DQ'd msg : %02X %02X %02X %02d %02d %s|\n", msg.ctrl, msg.dest, msg.src, msg.numHops, msg.len, msg.data);

        uint8_t *pkt;
        unsigned int pktSize = buildRoutingPacket(msg, &pkt);
        if (pkt == NULL)
        {
            // printf("%s - ### sendPackets_func: buildRoutingPacket failed\n", timestamp());
            free(pkt);
            // printf("%s - ### sendPackets_func: free NULL pkt\n", timestamp());
            continue; // Or handle error appropriately
        }

        // printf("%s - ### sendPackets_func pkt: ", timestamp());
        // for (unsigned int i = 0; i < pktSize; i++)
        // {
        //     printf("%02X ", pkt[i]);
        // }
        // printf("\n");

        // send
        if (MAC_send(macTemp, msg.next, pkt, pktSize))
        {
            // printf("%s - ### Done MAC_send : %02X (%s)\n", timestamp(), msg.next, msg.data);
        }
        else
        {
            // printf("%s - ### Failed MAC_send : %02X\n", timestamp(), msg.next);
        }
        // MAC_Isend(macTemp, msg.next, pkt, pktSize);
        // free(pkt);
        // printf("%s - ### sendPackets_func: free pkt\n", timestamp());

        // if (msg.data != NULL)
        // {
        //     free(msg.data);
        // printf("%s - ### sendPackets_func: free msg.data\n", timestamp());
        // }
    }
}

static int buildRoutingPacket(RoutingMessage msg, uint8_t **routePkt)
{
    uint16_t routePktSize = msg.len + headerSize;
    // printf("%s - ### buildRoutingPacket: malloc : %03d bytes\n", timestamp(), routePktSize);

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

    // Set numHops
    *p = msg.numHops;
    p += sizeof(msg.numHops);

    // Set actual msg length
    *p = msg.len;
    p += sizeof(msg.len);

    // Set msg
    memcpy(p, msg.data, msg.len);
    // p += msg.len;

    // free(routePkt);
    free(msg.data);
    // printf("%s - ### buildRoutingPacket: free \n", timestamp());
    // printf("%s - ### buildRoutingPacket: Packet contents: ", timestamp());
    // for (uint16_t i = 0; i < routePktSize; i++)
    // {
    //     printf("%02X ", (*routePkt)[i]);
    // }
    // printf("\n");
    return routePktSize;
}

static uint8_t getNextHopAddr(uint8_t self)
{
    uint8_t addr;
    unsigned short trial = 0;
    do
    {
        addr = NODE_POOL[rand() % POOL_SIZE];
        trial++;
    } while ((addr >= self || (addr - self) > 1) && trial <= maxTrials);

    return (trial > maxTrials) ? ADDR_SINK : addr;
}

static MAC initMAC(uint8_t addr, unsigned short debug, unsigned int timeout)
{
    // printf("%s - ### Inside initMAC\n", timestamp());
    GPIO_init();
    MAC macTemp;
    MAC_init(&macTemp, addr);
    macTemp.debug = debug;
    macTemp.recvTimeout = timeout;
    // macTemp.noiseThreshold = -65;
    return macTemp;
}

static void setDebug(uint8_t d)
{
    debugFlag = d;
}
