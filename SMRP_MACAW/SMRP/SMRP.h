#ifndef SMRP_H
#define SMRP_H
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <semaphore.h> // sem_init, sem_wait, sem_trywait, sem_timedwait


#include "../MACAW/MACAW.h"
#include "../common.h"

// Constants
// enums

typedef enum LinkType
{
    // Dependency : Graph generation script ../logs/script.py
    IDLE = 0,
    OUTBOUND = 2,
} LinkType;

typedef enum NodeState
{
    // Dependency : Graph generation script ../logs/script.py
    UNKNOWN = -1,
    INACTIVE = 0,
    ACTIVE = 1
} NodeState;

// Structs

typedef struct SMRP_Config
{
    // Node's own address
    uint8_t self;

    // Log level
    // Default INFO
    LogLevel loglevel;

    // Duration for neighbour sensing (seconds)
    // Default 15s
    unsigned int senseDurationS;

    // Interval between periodic beacons (seconds)
    // Default 30s
    unsigned int beaconIntervalS;

    // Neighbor keepalive timeout (seconds)
    // Default 60s
    unsigned int nodeTimeoutS;

    // Receive timeout for MAC layer (milliseconds)
    // Default 1000ms
    unsigned int recvTimeoutMs;

    // Number of maximum tries to find nexthop
    // Default 2
    uint8_t maxTries;

} SMRP_Config;

typedef struct Routing_Header
{
    uint8_t src;
    uint8_t dst;
    uint8_t prev;
    int RSSI;
} Routing_Header;

int SMRP_init(SMRP_Config config);
int SMRP_sendMsg(uint8_t dest, uint8_t *data, unsigned int len);
int SMRP_recvMsg(Routing_Header *header, uint8_t *data);
int SMRP_timedRecvMsg(Routing_Header *header, uint8_t *data, unsigned int timeout);

extern int (*Routing_sendMsg)(uint8_t dest, uint8_t *data, unsigned int len);
extern int (*Routing_recvMsg)(Routing_Header *h, uint8_t *data);
extern int (*Routing_timedRecvMsg)(Routing_Header *h, uint8_t *data, unsigned int timeout);
uint8_t Routing_getHeaderSize();
uint8_t Routing_isDataPkt(uint8_t ctrl);
uint8_t *Routing_getMetricsHeader();
uint8_t *Routing_getNeighbourHeader();
int Routing_getMetricsData(uint8_t *buffer, uint8_t addr);
int Routing_getNeighbourData(char *buffer, uint16_t size);
uint8_t Routing_getnextHop(uint8_t src, uint8_t prev, uint8_t dest, uint8_t maxTries);

#endif // SMRP_H