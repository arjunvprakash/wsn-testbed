#ifndef STRP_h
#define STRP_H

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
    ROLE_NEXTHOP,
    ROLE_CHILD,
    ROLE_NODE
} NodeRole;

typedef enum NodeState
{
    UNKNOWN,
    ACTIVE,
    INACTIVE
} NodeState;

// Structs

typedef struct
{
    uint8_t addr;
    int RSSI;
    unsigned short hopsToSink;
    time_t lastSeen;
    NodeRole role;
    uint8_t parent;
    NodeState state;
    int parentRSSI;
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

typedef struct NodeRoutingTable
{
    char *timestamp;
    uint8_t src;
    uint8_t numNodes;
    NodeInfo nodes[MAX_ACTIVE_NODES];
} NodeRoutingTable;

typedef struct STRP_Header
{
    uint8_t src;
    uint8_t dst;
    uint8_t prev;
    int RSSI;
    unsigned int numHops;
} STRP_Header;

typedef struct STRP_Config
{
    uint8_t self; // Node's own address
    LogLevel loglevel;
    ParentSelectionStrategy strategy;
    unsigned int senseDurationS;  // Duration for neighbour sensing (seconds)
    unsigned int beaconIntervalS; // Interval between periodic beacons (seconds)
    unsigned int nodeTimeoutS;    // Neighbor keepalive timeout (seconds)
    unsigned int recvTimeoutMs;   // Receive timeout for MAC layer (milliseconds)
} STRP_Config;

int STRP_init(STRP_Config config);
int STRP_sendMsg(uint8_t dest, uint8_t *data, unsigned int len);
int STRP_recvMsg(STRP_Header *header, uint8_t *data);
int STRP_timedRecvMsg(STRP_Header *header, uint8_t *data, unsigned int timeout);
int STRP_sendRoutingTable();
int STRP_timedRecvRoutingTable(STRP_Header *header, NodeRoutingTable *table, unsigned int timeout);
NodeRoutingTable STRP_recvRoutingTable(STRP_Header *header);
char *getNodeStateStr(NodeState state);
char *getNodeRoleStr(NodeRole role);
NodeRoutingTable getSinkRoutingTable();

#endif