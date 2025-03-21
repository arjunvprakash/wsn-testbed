#ifndef SMRP_H
#define SMRP_H
#pragma once

#include "../ALOHA/ALOHA.h"
#include "../common.h"

/**
 * @brief Supported roles for neighbour nodes
 * @note ProtoMon visualisation depends on these roles (ProtoMon/viz/script.py)
 */
typedef enum NodeRole
{
    ROLE_NODE = 0,
    ROLE_NEXTHOP = 2,
} NodeRole;

/**
 * @brief Supported states of neighbour nodes
 * @note ProtoMon visualisation depends on these states (ProtoMon/viz/script.py)
 */
typedef enum NodeState
{
    UNKNOWN = -1,
    INACTIVE = 0,
    ACTIVE = 1
} NodeState;

/**
 * @brief Configuration struct for SMRP protocol.
 */
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

/**
 * @brief Header information from the received message.
 */
typedef struct Routing_Header
{
    uint8_t src;
    uint8_t dst;
    uint8_t prev;
    int RSSI;
} Routing_Header;

/**
 * @brief Initialize the SMRP protocol.
 */
int SMRP_init(SMRP_Config config);
int SMRP_sendMsg(uint8_t dest, uint8_t *data, unsigned int len);
int SMRP_recvMsg(Routing_Header *header, uint8_t *data);
int SMRP_timedRecvMsg(Routing_Header *header, uint8_t *data, unsigned int timeout);

// To be able to be compatible with ProtoMon, the Routing protocol must declare the external function pointers

/**
 * @brief Send data to the Routing layer.
 *
 * @param dest Destination address (node identifier).
 * @param data Pointer to the data to send.
 * @param len Length of the data.
 * @return 1 on success, 0 on error.
 * @note Dependency with ProtoMon
 */
extern int (*Routing_sendMsg)(uint8_t dest, uint8_t *data, unsigned int len);

/**
 * @brief Receive data from the Routing layer.
 *
 * @param h Pointer to a Routing_Header structure.
 * @param data Pointer to a buffer for storing received message data.
 * @return Length of receoved message
 * @note Dependency with ProtoMon
 */
extern int (*Routing_recvMsg)(Routing_Header *h, uint8_t *data);

/**
 * @brief Receive data from the Routing layer with a timeout.
 *
 * @param h Pointer to a Routing_Header structure with message metadata.
 * @param data Pointer to a buffer for storing received message data.
 * @param timeout Maximum wait time for a message (in milliseconds).
 * @return Length of receoved message
 * @note Dependency with ProtoMon
 */
extern int (*Routing_timedRecvMsg)(Routing_Header *h, uint8_t *data, unsigned int timeout);

/**
 * @returns size of the SMRP packet header
 * @note Dependency with ProtoMon
 */
uint8_t Routing_getHeaderSize();

/**
 * @brief Check if a packet is an SMRP data packet.
 * @param ctrl control flag of the packet
 * @returns 1 if the control flag is of an SMRP data packet
 * @note Dependency with ProtoMon
 */
uint8_t Routing_isDataPkt(uint8_t ctrl);

/**
 * @returns CSV header of metrics collected by SMRP.
 * @note Dependency with ProtoMon
 */
uint8_t *Routing_getMetricsHeader();

/**
 * @returns ""
 * @note Dependency with ProtoMon
 */
uint8_t *Routing_getNeighbourHeader();

/**
 * @returns CSV data of metrics collected by STRP.
 * @note Dependency with ProtoMon
 */
int Routing_getMetricsData(uint8_t *buffer, uint8_t addr);

/**
 * @returns CSV data of neighbour nodes
 * @note Dependency with ProtoMon
 */
int Routing_getNeighbourData(char *buffer, uint16_t size);

/**
 * @brief Get the address of a random active node from neighbour list.
 */
uint8_t Routing_getnextHop(uint8_t src, uint8_t prev, uint8_t dest, uint8_t maxTries);

#endif // SMRP_H