#ifndef TOPOMAP_H
#define TOPOMAP_H

#include <stdint.h>
#include "../STRP/STRP.h"

// Structs
typedef struct NodeRoutingTable
{
    char *timestamp;
    uint8_t src;
    uint8_t numActive;
    NodeInfo nodes[MAX_ACTIVE_NODES];
} NodeRoutingTable;

typedef struct TableQueue
{
    NodeRoutingTable table[MAX_ACTIVE_NODES];
    sem_t mutex, full, free;
    unsigned int begin, end;
} TableQueue;

// Function Declarations

void topomapInitialize();
void writeToCSVFile(NodeRoutingTable table);


#endif