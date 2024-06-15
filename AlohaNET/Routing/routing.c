#include "ALOHA.h"

#include <errno.h>     // errno
#include <pthread.h>   // pthread_create
#include <semaphore.h> // sem_init, sem_wait, sem_trywait, sem_timedwait
#include <stdbool.h>   // bool, true, false
#include <stdio.h>     // printf
#include <stdlib.h>    // rand, malloc, free, exit
#include <string.h>    // memcpy, strerror

#include "routing.h"
#include "../common.h"
#include "../util.h"

typedef struct RoutingMessage
{
    uint8_t src;
    uint8_t prev;
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

static RoutingQueue fwdQ, recvQ;

static pthread_t recvT;
static pthread_t sendT;
static MAC mac;

int routing_init(uint8_t self, unsigned short debug)
{
    mac = initNode(self, debug);
}

int routing_send(uint8_t dest, uint8_t *data, unsigned int len)
{
    RoutingMessage msg;
    msg.len = len;
    msg.dest = dest;
    msg.next = getNextHopAddr(mac.addr);
    msg.src = mac.addr;
    memcpy(msg.data, data, len);

    fwdMsgQ_enqueue(msg);

    return 1;
}

int routing_recv(unsigned char *data, RouteHeader *header)
{
    RoutingMessage msg = recvMsgQ_dequeue();
    header->dst = msg.dest;
    header->RSSI = mac.RSSI;
    header->src = msg.src;
    *data = msg.data;
    return 1;
}

void fwdQ_init()
{
    fwdQ.begin = 0;
    fwdQ.end = 0;

    sem_init(&fwdQ.full, 0, 0);
    sem_init(&fwdQ.free, 0, RoutingQueueSize);
    sem_init(&fwdQ.mutex, 0, 1);
}

void recvQ_init()
{
    recvQ.begin = 0;
    recvQ.end = 0;

    sem_init(&recvQ.full, 0, 0);
    sem_init(&recvQ.free, 0, RoutingQueueSize);
    sem_init(&recvQ.mutex, 0, 1);
}

void fwdMsgQ_enqueue(RoutingMessage msg)
{
    sem_wait(&fwdQ.free);
    sem_wait(&fwdQ.mutex);

    fwdQ.packet[fwdQ.end] = msg;
    fwdQ.end = (fwdQ.end + 1) % RoutingQueueSize;

    sem_post(&fwdQ.mutex);
    sem_post(&fwdQ.full);
}

RoutingMessage fwdMsgQ_dequeue()
{
    sem_wait(&fwdQ.full);
    sem_wait(&fwdQ.mutex);

    RoutingMessage msg = fwdQ.packet[fwdQ.begin];
    fwdQ.begin = (fwdQ.begin + 1) % RoutingQueueSize;

    sem_post(&fwdQ.mutex);
    sem_post(&fwdQ.full);
}

void recvMsgQ_enqueue(RoutingMessage msg)
{
    sem_wait(&recvQ.free);
    sem_wait(&recvQ.mutex);

    recvQ.packet[recvQ.end] = msg;
    recvQ.end = (recvQ.end + 1) % RoutingQueueSize;

    sem_post(&recvQ.mutex);
    sem_post(&recvQ.full);
}

RoutingMessage recvMsgQ_dequeue()
{
    sem_wait(&recvQ.full);
    sem_wait(&recvQ.mutex);

    RoutingMessage msg = recvQ.packet[recvQ.begin];
    recvQ.begin = (recvQ.begin + 1) % RoutingQueueSize;

    sem_post(&recvQ.mutex);
    sem_post(&recvQ.full);
}

void recvPackets_func(void *args)
{
    MAC *mac = (MAC *)args;
    while (1)
    {
        uint8_t *pkt = (uint8_t *)malloc(240);

        // Receive a packetF
        int pktSize = MAC_recv(mac, pkt);
        RoutingMessage msg = buildRoutingMessage(pkt);
        free(pkt);
        if (msg.dest != mac->addr)
        {
            // Enqueue the message into the forward queue
            fwdMsgQ_enqueue(msg);
        }
        else
        {
            recvMsgQ_enqueue(msg);
        }
    }
}

RoutingMessage buildRoutingMessage(uint8_t *)
{
    // Construct RoutingMessage
    RoutingMessage msg;
    msg.src = pkt[1];
    msg.prev = mac->recvH.src_addr;
    msg.dest = pkt[0];
    msg.next = getNextHopAddr(mac->addr);
    msg.len = pkt[2];
    msg.data = (uint8_t *)malloc(msg.len);
    memcpy(msg.data, pkt[3], msg.len);
    free(pkt);

    return msg;
}

void sendPackets_func(void *args)
{
    MAC *mac = (MAC *)args;

    while (1)
    {
        RoutingMessage msg = fwdMsgQ_dequeue();

        uint8_t *pkt;
        int pktSize = buildRoutingPacket(msg, &pkt);
        // send
        MAC_send(&mac, msg.next, pkt, pktSize);
    }
}

int buildRoutingPacket(RoutingMessage msg, uint8_t **routePkt)
{
    uint16_t routePktSize = msg.len + 3;
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
    free(routePkt);

    return routePktSize;
}
