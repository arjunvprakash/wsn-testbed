#include <errno.h>     // errno
#include <pthread.h>   // pthread_create
#include <semaphore.h> // sem_init, sem_wait, sem_trywait, sem_timedwait
#include <stdbool.h>   // bool, true, false
#include <stdio.h>     // printf
#include <stdlib.h>    // rand, malloc, free, exit
#include <string.h>    // memcpy, strerror

#include "routing.h"
#include "../ALOHA/ALOHA.h"
#include "../GPIO/GPIO.h"
#include "../common.h"
#include "../util.h"

typedef struct RoutingMessage
{
    uint8_t src;
    uint8_t prev;
    unsigned int numHops;
    uint8_t dest;
    uint8_t next;
    uint8_t *data;
    uint16_t len;
} RoutingMessage;

#define RoutingQueueSize 16

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
static uint8_t debug;
static const unsigned short maxTrials = 4;
static const unsigned short numHeaderFields = 4;

void sendQ_init();
void recvQ_init();
void sendQ_enqueue(RoutingMessage msg);
RoutingMessage sendQ_dequeue();
void recvQ_enqueue(RoutingMessage msg);
RoutingMessage recvMsgQ_dequeue();
void *recvPackets_func(void *args);
RoutingMessage buildRoutingMessage(uint8_t *pkt);
void *sendPackets_func(void *args);
int buildRoutingPacket(RoutingMessage msg, uint8_t **routePkt);
uint8_t getNextHopAddr(uint8_t self);
static MAC initMAC(uint8_t addr, unsigned short debug, unsigned int timeout);
void setDebug(uint8_t d);
void recvMsgQ_timed_dequeue(RoutingMessage *msg, struct timespec *ts);

// Initialize the routing layer
int routingInit(uint8_t self, uint8_t debug, unsigned int timeout)
{
    printf("%s - ### Inside routingInit\n", timestamp());
    setDebug(debug);
    mac = initMAC(self, debug, timeout);
    sendQ_init();
    recvQ_init();

    if (pthread_create(&recvT, NULL, recvPackets_func, &mac) != 0)
    {
        printf("Failed to create Routing receive thread");
        exit(1);
    }
    printf("%s - ### Routing : recvT created\n", timestamp());

    if (pthread_create(&sendT, NULL, sendPackets_func, &mac) != 0)
    {
        printf("Failed to create Routing send thread");
        exit(1);
    }
    printf("%s - ### Routing : sendT created\n", timestamp());
    return 1;
}

// Send a message via the routing layer
int routingSend(uint8_t dest, uint8_t *data, unsigned int len)
{
    RoutingMessage msg;
    msg.len = len;
    msg.dest = dest;
    msg.next = getNextHopAddr(mac.addr);
    msg.src = mac.addr;
    msg.data = (uint8_t *)malloc(len);
    if (msg.data != NULL)
    {
        memcpy(msg.data, data, len);
    }
    else
    {
        // Handle memory allocation failure
        printf("%s - ### routingSend: memory allocation failure\n", timestamp());
    }
    memcpy(msg.data, data, len);

    sendQ_enqueue(msg);

    return 1;
}

// Receive a message via the routing layer
int routingReceive(RouteHeader *header, uint8_t *data)
{
    printf("%s - ### Inside routingReceive\n", timestamp());
    RoutingMessage msg = recvMsgQ_dequeue();
    header->dst = msg.dest;
    header->RSSI = mac.RSSI;
    header->src = msg.src;
    if (msg.data != NULL)
    {
        memcpy(data, msg.data, msg.len);
    }
    else
    {
        printf("%s - ### routingReceive: data is NULL\n", timestamp());
    }
    return msg.len;
}

// Receive a message via the routing layer
int routingTimedReceive(RouteHeader *header, uint8_t *data, unsigned int timeout)
{
    printf("%s - ### Inside routingTimedReceive\n", timestamp());
    RoutingMessage msg;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout;
    recvMsgQ_timed_dequeue(&msg, &ts);
    header->dst = msg.dest;
    header->RSSI = mac.RSSI;
    header->src = msg.src;
    if (msg.data != NULL)
    {
        memcpy(data, msg.data, msg.len);
    }
    else
    {
        // Handle null pointer case
        printf("%s - ### routingTimedReceive: data is NULL\n", timestamp());
    }
    // *data = *msg.data;
    return msg.len;
}

void recvMsgQ_timed_dequeue(RoutingMessage *msg, struct timespec *ts)
{
    void recvMsgQ_timed_dequeue(RoutingMessage * msg, struct timespec * ts)
    {
        printf("%s - ### Inside recvMsgQ_timed_dequeue\n", timestamp());
        if (sem_timedwait(&recvQ.full, ts) == -1)
        {
            // Handle timeout or error
            return;
        }

        sem_wait(&recvQ.mutex);
        *msg = recvQ.packet[recvQ.begin];
        recvQ.begin = (recvQ.begin + 1) % RoutingQueueSize;
        sem_post(&recvQ.mutex);
        sem_post(&recvQ.full);
    }

    // printf("%s - ### Inside recvMsgQ_timed_dequeue\n", timestamp());

    // if (sem_timedwait(&recvQ.full, ts) == -1)
    //     return;

    // sem_wait(&recvQ.mutex);

    // *msg = recvQ.packet[recvQ.begin];
    // recvQ.begin = (recvQ.begin + 1) % RoutingQueueSize;

    // sem_post(&recvQ.mutex);
    // sem_post(&recvQ.full);
}

void sendQ_init()
{
    printf("%s - ### Inside recvQ_init\n", timestamp());

    sendQ.begin = 0;
    sendQ.end = 0;

    sem_init(&sendQ.full, 0, 0);
    sem_init(&sendQ.free, 0, RoutingQueueSize);
    sem_init(&sendQ.mutex, 0, 1);
}

void recvQ_init()
{
    printf("%s - ### Inside recvQ_init\n", timestamp());

    recvQ.begin = 0;
    recvQ.end = 0;

    sem_init(&recvQ.full, 0, 0);
    sem_init(&recvQ.free, 0, RoutingQueueSize);
    sem_init(&recvQ.mutex, 0, 1);
}

void sendQ_enqueue(RoutingMessage msg)
{
    printf("%s - ### Inside sendQ_enqueue\n", timestamp());

    sem_wait(&sendQ.free);
    sem_wait(&sendQ.mutex);

    sendQ.packet[sendQ.end] = msg;
    sendQ.end = (sendQ.end + 1) % RoutingQueueSize;

    sem_post(&sendQ.mutex);
    sem_post(&sendQ.full);
}

RoutingMessage sendQ_dequeue()
{
    printf("%s - ### Inside sendQ_dequeue\n", timestamp());

    sem_wait(&sendQ.full);
    sem_wait(&sendQ.mutex);

    RoutingMessage msg = sendQ.packet[sendQ.begin];
    sendQ.begin = (sendQ.begin + 1) % RoutingQueueSize;

    sem_post(&sendQ.mutex);
    sem_post(&sendQ.full);
}

void recvQ_enqueue(RoutingMessage msg)
{
    printf("%s - ### Inside recvQ_enqueue\n", timestamp());

    sem_wait(&recvQ.free);
    sem_wait(&recvQ.mutex);

    recvQ.packet[recvQ.end] = msg;
    recvQ.end = (recvQ.end + 1) % RoutingQueueSize;

    sem_post(&recvQ.mutex);
    sem_post(&recvQ.full);
}

RoutingMessage recvMsgQ_dequeue()
{
    printf("%s - ### Inside recvMsgQ_dequeue\n", timestamp());
    sem_wait(&recvQ.full);
    sem_wait(&recvQ.mutex);

    RoutingMessage msg = recvQ.packet[recvQ.begin];
    recvQ.begin = (recvQ.begin + 1) % RoutingQueueSize;

    sem_post(&recvQ.mutex);
    sem_post(&recvQ.full);
}

void *recvPackets_func(void *args)
{
    printf("%s - ### Inside recvPackets_func\n", timestamp());

    MAC *mac = (MAC *)args;
    while (1)
    {
        printf("%s - ### Loop recvPackets_func\n", timestamp());

        uint8_t *pkt = (uint8_t *)malloc(240);

        // Receive a packet
        // int pktSize = MAC_recv(mac, pkt);
        int pktSize = MAC_timedrecv(mac, pkt, 3);

        RoutingMessage msg = buildRoutingMessage(pkt);
        free(pkt);
        printf("%s - ### Received %s -> %s msg: %s\n", timestamp(), msg.src, msg.dest, msg.data);

        if (msg.dest == ADDR_BROADCAST || msg.dest == mac->addr)
        {
            // Keep
            recvQ_enqueue(msg);
        }
        else
        {
            // Forward
            msg.next = getNextHopAddr(mac->addr);
            msg.numHops++;
            if (MAC_send(mac, msg.next, pkt, pktSize))
            {
                if (debug)
                {
                    printf("%s - FWD: %02X -> %02X msg: %04d\n", timestamp(), msg.src, msg.next, msg.data);
                }
            }
            else
            {
                printf("%s - ## Error forwarding: %02X -> %02X msg: %04d\n", timestamp(), msg.src, msg.next, msg.data);
            }
        }
        free(msg.data);
    }
}

// Construct RoutingMessage
RoutingMessage buildRoutingMessage(uint8_t *pkt)
{
    printf("%s - ### Inside buildRoutingMessage\n", timestamp());

    RoutingMessage msg;
    msg.src = pkt[1];
    msg.prev = mac.recvH.src_addr;
    msg.dest = pkt[0];
    msg.len = pkt[2];
    if (msg.len > 0)
    {
        msg.data = (uint8_t *)malloc(msg.len);
    }
    else
    {
        msg.data = NULL;
    }
    memcpy(msg.data, &pkt[3], msg.len);
    msg.numHops = pkt[4];
    return msg;
}

void *sendPackets_func(void *args)
{
    MAC *mac = (MAC *)args;

    while (1)
    {
        RoutingMessage msg = sendQ_dequeue();

        uint8_t *pkt;
        int pktSize = buildRoutingPacket(msg, &pkt);
        // send
        MAC_send(mac, msg.next, pkt, pktSize);
    }
}

int buildRoutingPacket(RoutingMessage msg, uint8_t **routePkt)
{
    uint16_t routePktSize = msg.len + numHeaderFields;
    *routePkt = (uint8_t *)malloc(routePktSize);

    uint8_t *p = *routePkt;

    // Set dest
    *p = msg.dest;
    p += sizeof(uint8_t);

    // Set source as self
    *p = mac.addr;
    p += sizeof(uint8_t);

    // Set actual msg length
    *p = msg.len;
    p += sizeof(uint8_t);

    // Set msg
    memcpy(p, msg.data, msg.len);
    p += msg.len;

    // Set numHops
    *p = msg.numHops;
    // free(routePkt);
    free(msg.data);

    return routePktSize;
}

uint8_t getNextHopAddr(uint8_t self)
{
    uint8_t addr;
    unsigned short trial = 0;
    do
    {
        addr = NODE_POOL[rand() % POOL_SIZE];
        trial++;
    } while (addr >= self && trial <= maxTrials);

    return (trial > maxTrials) ? ADDR_SINK : addr;
}

static MAC initMAC(uint8_t addr, unsigned short debug, unsigned int timeout)
{
    printf("%s - ### Inside initMAC\n", timestamp());
    GPIO_init();
    MAC mac;
    MAC_init(&mac, addr);
    mac.debug = debug;
    mac.recvTimeout = timeout;
    return mac;
}

void setDebug(uint8_t d)
{
    debug = debug;
}
