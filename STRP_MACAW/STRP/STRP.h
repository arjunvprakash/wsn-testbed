#ifndef STRP_h
#define STRP_H
#pragma once

#include "../MACAW/MACAW.h"
#include "../common.h"

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

/**
 * @brief Supported roles for neighbour nodes
 * @note ProtoMon visualisation depends on these roles (ProtoMon/viz/script.py)
 */
typedef enum Routing_LinkType
{
    IDLE = 0,
    INBOUND = 1, // Needs additional handling in ProtoMon/viz/script.py
    OUTBOUND = 2,
} Routing_LinkType;

/**
 * @brief Supported states of neighbour nodes
 * @note ProtoMon visualisation depends on these states (ProtoMon/viz/script.py)
 */
typedef enum Routing_NodeState
{
    UNKNOWN = -1,
    INACTIVE = 0,
    ACTIVE = 1
} Routing_NodeState;

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
 * @brief Header information from the received message.
 */
typedef struct Routing_Header
{
    uint8_t src;  // Source of the packet
    uint8_t dst;  // Destination of the packet
    uint8_t prev; // Address of the previous hop
    int RSSI;     // RSSI of the previous hop address
} Routing_Header;

/**
 * @brief Initialize the STRP protocol.
 */
int STRP_init(STRP_Config config);
int STRP_sendMsg(uint8_t dest, uint8_t *data, unsigned int len);
int STRP_recvMsg(Routing_Header *header, uint8_t *data);
int STRP_timedRecvMsg(Routing_Header *header, uint8_t *data, unsigned int timeout);

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
 * @brief Receive data from the Routing layer. Blocking operation.
 *
 * @param h Pointer to a Routing_Header structure.
 * @param data Pointer to a buffer for storing received message data.
 * @return Length of receoved message
 * @note Dependency with ProtoMon
 */
extern int (*Routing_recvMsg)(Routing_Header *h, uint8_t *data);

/**
 * @brief Receive data from the Routing layer with a timeout. Non-blocking operation.
 *
 * @param h Pointer to a Routing_Header structure with message metadata.
 * @param data Pointer to a buffer for storing received message data.
 * @param timeout Maximum wait time for a message (in milliseconds).
 * @return length of received message on success, -1 for timeout
 * @note Dependency with ProtoMon
 */
extern int (*Routing_timedRecvMsg)(Routing_Header *h, uint8_t *data, unsigned int timeout);

/**
 * @returns size of the STRP packet header
 * @note Dependency with ProtoMon
 */
uint8_t Routing_getHeaderSize();

/**
 * @brief Check if a packet is an STRP data packet.
 * @param ctrl control flag of the packet
 * @returns 1 if the control flag is of an STRP data packet
 * @note Dependency with ProtoMon
 */
uint8_t Routing_isDataPkt(uint8_t ctrl);

/**
 * @returns CSV header of metrics collected by STRP.
 * @note Dependency with ProtoMon
 */
uint8_t *Routing_getMetricsHeader();

/**
 * @returns CSV header for additional fields of neighbour data.
 * @note Dependency with ProtoMon
 */
uint8_t *Routing_getTopologyHeader();

/**
 * @returns CSV data of metrics collected by STRP.
 * @note Dependency with ProtoMon
 */
int Routing_getMetricsData(uint8_t *buffer, uint8_t addr);

/**
 * @returns CSV data of neighbour nodes
 * @note Dependency with ProtoMon
 */
int Routing_getTopologyData(char *buffer, uint16_t size);

#endif // STRP_H