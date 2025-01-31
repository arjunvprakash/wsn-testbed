#ifndef TOPOMAP_H
#define TOPOMAP_H

#include <stdint.h>

#include "../common.h"

// Structs

typedef struct TopoMap_Config
{
    uint8_t self;
    LogLevel loglevel;
    unsigned int routingTableIntervalS; // Interval to send routing table to sink
    unsigned int graphUpdateIntervalS;  // Interval to generate graph
} TopoMap_Config;

// Function Declarations
void TopoMap_init(TopoMap_Config config);

#endif