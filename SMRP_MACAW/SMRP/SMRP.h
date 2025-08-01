#ifndef SMRP_H
#define SMRP_H
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <semaphore.h> // sem_init, sem_wait, sem_trywait, sem_timedwait

#include "../MACAW/MACAW.h"
#include "../common.h"
#include "../ProtoMon/routing.h"

// Constants
// enums

// Structs
typedef struct Routing_Header
{
    t_addr src;  // Source of the packet
    t_addr dst;  // Destination of the packet
    t_addr prev; // Address of the previous hop
    int8_t RSSI;  // RSSI of the previous hop address
} Routing_Header;

/**
 * @brief Configuration struct for SMRP protocol.
 */
typedef struct SMRP_Config
{
    // Node's own address
    t_addr self;

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

    MAC *mac;

    // Number of maximum tries to find nexthop
    // Default 2
    uint8_t maxTries;

} SMRP_Config;

/**
 * @brief Initialize the SMRP protocol.
 */
int SMRP_init(SMRP_Config config);
int SMRP_sendMsg(uint8_t dest, uint8_t *data, unsigned int len);
int SMRP_recvMsg(Routing_Header *header, uint8_t *data);
int SMRP_timedRecvMsg(Routing_Header *header, uint8_t *data, unsigned int timeout);

/**
 * @brief Get the address of a random active node from neighbour list.
 */
t_addr Routing_getnextHop(t_addr src, t_addr prev, t_addr dest, uint8_t maxTries);

#endif // SMRP_H