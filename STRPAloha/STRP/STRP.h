#ifndef STRP_h
#define STRP_H
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <semaphore.h> // sem_init, sem_wait, sem_trywait, sem_timedwait

#include "../ALOHA/ALOHA.h"
#include "../common.h"

// Constants
// enums
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
    // Dependency : Graph generation script ../logs/script.py
    ROLE_NODE = 0,
    ROLE_CHILD = 1,
    ROLE_NEXTHOP = 2,
} NodeRole;

typedef enum NodeState
{
    // Dependency : Graph generation script ../logs/script.py
    UNKNOWN = -1,
    INACTIVE = 0,
    ACTIVE = 1
} NodeState;

// Structs


typedef struct STRP_Config
{
    // Node's own address
    uint8_t self; 

    // Log level
    // Default INFO
    LogLevel loglevel;

    // Default CLOSEST
    ParentSelectionStrategy strategy;

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

} STRP_Config;

typedef struct Routing_Header
{
    uint8_t src;
    uint8_t dst;
    uint8_t prev;
    int RSSI;
} Routing_Header;

int STRP_init(STRP_Config config);
int STRP_sendMsg(uint8_t dest, uint8_t *data, unsigned int len);
int STRP_recvMsg(Routing_Header *header, uint8_t *data);
int STRP_timedRecvMsg(Routing_Header *header, uint8_t *data, unsigned int timeout);

char *getNodeStateStr(NodeState state);
char *getNodeRoleStr(NodeRole role);

extern int (*Routing_sendMsg)(uint8_t dest, uint8_t *data, unsigned int len);
extern int (*Routing_recvMsg)(Routing_Header *h, uint8_t *data);
extern int (*Routing_timedRecvMsg)(Routing_Header *h, uint8_t *data, unsigned int timeout);
uint8_t Routing_getHeaderSize();
uint8_t Routing_isDataPkt(uint8_t ctrl);
uint8_t *Routing_getMetricsHeader();
int Routing_getMetricsData(uint8_t *buffer, uint8_t addr);
int Routing_getNeighbourData(char *buffer, uint16_t size);

#endif // STRP_H