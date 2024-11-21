#ifndef STRP_h
#define STRP_H

#include <stdint.h>
#include <stdbool.h>
#include <semaphore.h>
#include <time.h>

#include "../ALOHA/ALOHA.h"

// Constants
#define MAX_ACTIVE_NODES 32

// enums
typedef enum ParentSelectionStrategy
{
    RANDOM_LOWER,
    RANDOM,
    NEXT_LOWER,
    CLOSEST,
    CLOSEST_LOWER
} ParentSelectionStrategy;


// Structs

typedef struct {
    uint8_t addr;
    int RSSI;
    unsigned short hopsToSink;
    time_t lastSeen;
    NodeRole role;
    uint8_t parent;
    bool isActive;
    int parentRSSI;
} NodeInfo;

typedef struct {
    NodeInfo nodes[MAX_ACTIVE_NODES];
    sem_t mutex;
    uint8_t numActive;
    time_t lastCleanupTime;
    uint8_t minAddr, maxAddr;
} ActiveNodes;


typedef struct RouteHeader
{
    uint8_t src;
    uint8_t dst;
    uint8_t prev;
    int RSSI;
    unsigned int numHops;
} RouteHeader;

int routingInit(uint8_t self, uint8_t debug, unsigned int timeout);
int routingSend(uint8_t dest, uint8_t *data, unsigned int len);
int routingReceive(RouteHeader *header, uint8_t *data);
int routingTimedReceive(RouteHeader *header, uint8_t *data, unsigned int timeout);
char *getRoleStr(NodeRole role);

#endif