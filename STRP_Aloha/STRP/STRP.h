#ifndef STRP_h
#define STRP_H
#pragma once

#include "../ALOHA/ALOHA.h"
#include "../common.h"
#include "../ProtoMon/routing.h"


typedef struct MAC MAC;

/**
 * @brief Supported parent selection strategies
 *
 */
typedef enum ParentSelectionStrategy
{
    RANDOM_LOWER,  // Choose a random neighbor with address lower than self
    RANDOM,        // Choose a random neighbor
    NEXT_LOWER,    // Choose the neighbor with next lower address than self
    CLOSEST,       // Choose the neighbor with least RSSI
    CLOSEST_LOWER, // Choose the closest neighbor with address lower than self
    FIXED          // Use the parent assignment in the config
} ParentSelectionStrategy;

typedef struct Routing_Header
{
    uint8_t src;  // Source of the packet
    uint8_t dst;  // Destination of the packet
    uint8_t prev; // Address of the previous hop
    int8_t RSSI;     // RSSI of the previous hop address
} Routing_Header;

/**
 * @brief Configuration struct for STRP protocol.
 */
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
    // unsigned int recvTimeoutMs;
    
    MAC *mac;

    // Pointer to parent array of size MAX_ACTIVE_NODES indexed by node address
    uint8_t parentAddr;

} STRP_Config;

/**
 * @brief Initialize the STRP protocol.
 */
int STRP_init(STRP_Config config);
int STRP_sendMsg(uint8_t dest, uint8_t *data, unsigned int len);
int STRP_recvMsg(Routing_Header *header, uint8_t *data);
int STRP_timedRecvMsg(Routing_Header *header, uint8_t *data, unsigned int timeout);

#endif // STRP_H