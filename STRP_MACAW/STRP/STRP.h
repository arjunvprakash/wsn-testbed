#ifndef STRP_h
#define STRP_H
#pragma once

#include "../MACAW/MACAW.h"
#include "../common.h"
#include "../ProtoMon/routing.h"

/**
 * @brief Supported parent selection strategies
 *
 */
typedef enum ParentSelectionStrategy
{
    RANDOM_LOWER,
    RANDOM,
    NEXT_LOWER,
    CLOSEST,
    CLOSEST_LOWER
} ParentSelectionStrategy;

typedef struct Routing_Header
{
    uint8_t src;  // Source of the packet
    uint8_t dst;  // Destination of the packet
    uint8_t prev; // Address of the previous hop
    int RSSI;     // RSSI of the previous hop address
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
    unsigned int recvTimeoutMs;

} STRP_Config;


/**
 * @brief Initialize the STRP protocol.
 */
int STRP_init(STRP_Config config);
int STRP_sendMsg(uint8_t dest, uint8_t *data, unsigned int len);
int STRP_recvMsg(Routing_Header *header, uint8_t *data);
int STRP_timedRecvMsg(Routing_Header *header, uint8_t *data, unsigned int timeout);

#endif // STRP_H