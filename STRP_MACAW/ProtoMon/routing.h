#ifndef ROUTING_H
#define ROUTING_H
#pragma once

#include <stdint.h>

/**
 * @brief Header information from the received message.
 * @note Must contain fields
 * @note     uint8_t src;  // Source of the packet
 * @note     uint8_t dst;  // Destination of the packet
 * @note     uint8_t prev; // Address of the previous hop
 * @note     int RSSI;     // RSSI of the previous hop address
 */
typedef struct Routing_Header Routing_Header;

/**
 * @brief Supported link types for neighbor nodes
 * @note ProtoMon visualisation depends on these roles (ProtoMon/viz/script.py)
 */
typedef enum Routing_LinkType
{
    IDLE = 0,
    INBOUND = 1, // Needs additional handling in ProtoMon/viz/script.py
    OUTBOUND = 2,
    INOUTBOUND = 3,
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

// To be able to be compatible with ProtoMon, the Routing protocol must declare the external function pointers

/**
 * @brief Send data to the Routing layer.
 *
 * @param dest Destination address (node identifier).
 * @param data Pointer to the data to send.
 * @param len Length of the data.
 * @return 1 on success, 0 on error.
 */
extern int (*Routing_sendMsg)(uint8_t dest, uint8_t *data, unsigned int len);

/**
 * @brief Receive data from the Routing layer. Blocking operation.
 *
 * @param h Pointer to a Routing_Header structure.
 * @param data Pointer to a buffer for storing received message data.
 * @return Length of receoved message
 */
extern int (*Routing_recvMsg)(Routing_Header *h, uint8_t *data);

/**
 * @brief Receive data from the Routing layer with a timeout. Non-blocking operation.
 *
 * @param h Pointer to a Routing_Header structure with message metadata.
 * @param data Pointer to a buffer for storing received message data.
 * @param timeout Maximum wait time for a message (in milliseconds).
 * @return length of received message on success, -1 for timeout
 */
extern int (*Routing_timedRecvMsg)(Routing_Header *h, uint8_t *data, unsigned int timeout);

/**
 * @returns size of the routing header
 * @note Dependency with ProtoMon
 */
uint8_t Routing_getHeaderSize();

/**
 * @brief Check if a packet is a data packet.
 * @param ctrl control flag of the packet
 * @returns 1 if the control flag is of a data packet
 * @note Dependency with ProtoMon
 */
uint8_t Routing_isDataPkt(uint8_t ctrl);

/**
 * @returns CSV header of metrics collected by Routing protocol.
 * @note Dependency with ProtoMon
 */
uint8_t *Routing_getMetricsHeader();

/**
 * @returns CSV header for fields of neighbour data.
 * @note Dependency with ProtoMon
 * @note Must contain fields : [Timestamp,Source,Address,State,LinkType,RSSI] irrespective of order
 * @note Additional fields must be explicitly handled in the ProtoMon/viz/script.py
 */
uint8_t *Routing_getTopologyHeader();

/**
 * @returns CSV data of metrics collected by Routing protocol.
 * @note Dependency with ProtoMon
 */
int Routing_getMetricsData(uint8_t *buffer, uint8_t addr);

/**
 * @returns CSV data of neighbour nodes
 * @note Dependency with ProtoMon
 */
int Routing_getTopologyData(char *buffer, uint16_t size);

#endif // ROUTING_H