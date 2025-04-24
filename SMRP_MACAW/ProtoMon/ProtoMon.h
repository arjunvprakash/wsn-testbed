#ifndef ProtoMon_H
#define ProtoMon_H
#pragma once

#include "../common.h"
#include "../MACAW/MACAW.h" // include the MAC layer implemetation
#include "../SMRP/SMRP.h"   // include the routing layer implemetation

/**
 * @brief Configuration struct for ProtoMon.
 *
 */
typedef struct ProtoMon_Config
{
    // Node's own address
    uint8_t self;

    // Log level
    // Default INFO
    LogLevel loglevel;

    // Interval to send monitoring metrics to sink
    // Default 30s
    unsigned int sendIntervalS;

    // Interval to generate visualization
    // Default 60s
    unsigned int vizIntervalS;

    // Bitmask for layers to monitor
    uint8_t monitoredLevels;

    // Initial wait time before sending first monitoring data packet.
    // Could be used to wait for lower layers to initialize.
    // Default 30s
    uint16_t initialSendWaitS;
} ProtoMon_Config;

/**
 * @brief Protocol layer combinations supported for monitoring by ProtoMon.
 *
 */
typedef enum ProtoMon_Level
{
    PROTOMON_LEVEL_NONE = 0x00,    // No monitoring
    PROTOMON_LEVEL_MAC = 0x01,     // Monitor MAC
    PROTOMON_LEVEL_ROUTING = 0x02, // Monitor Routing
    PROTOMON_LEVEL_TOPO = 0x04,    // Monitor Topology
    PROTOMON_LEVEL_ALL = 0xFF      // Monitor all layers
} ProtoMon_Level;

/**
 * @brief Initialize the Monitoring layer. Must be called BEFORE initializing the lower layers.
 * @param config Configuration
 * @return None.
 */
void ProtoMon_init(ProtoMon_Config config);

#endif // STRP_H