#ifndef ProtoMon_H
#define ProtoMon_H
#pragma once

#include "../common.h"
#include "../MACAW/MACAW.h" // include the MAC layer implemetation
#include "../STRP/STRP.h"   // include the routing layer implemetation

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
    uint8_t monitoredLayers;

    // Initial wait time before sending first monitoring data packet.
    // Could be used to wait for lower layers to initialize.
    // Default 30s
    uint16_t initialSendWaitS;
} ProtoMon_Config;

/**
 * @brief Protocol layer combinations supported for monitoring by ProtoMon.
 *
 */
typedef enum ProtoMon_Layer
{
    PROTOMON_LAYER_NONE = 0x00,    // No monitoring
    PROTOMON_LAYER_MAC = 0x01,     // Monitor MAC
    PROTOMON_LAYER_ROUTING = 0x02, // Monitor Routing
    PROTOMON_LAYER_TOPO = 0x04,    // Monitor Topology
    PROTOMON_LAYER_ALL = 0xFF      // Monitor all layers
} ProtoMon_Layer;

/**
 * @brief Initialize the Monitoring layer. Must be called BEFORE initializing the lower layers.
 * @param config Configuration
 * @return None.
 */
void ProtoMon_init(ProtoMon_Config config);

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

// To be able to be compatible with ProtoMon, the MAC protocol must declare these external function pointers

/**
 * @brief Send data to the MAC layer.
 *
 * @param h Pointer to a MAC config.
 * @param dest Destination address (node identifier).
 * @param data Pointer to the data to send.
 * @param len Length of the data.
 * @return 1 on success, 0 on error.
 */
extern int (*MAC_send)(MAC *h, unsigned char dest, unsigned char *data, unsigned int len);

/**
 * @brief Receive data from the MAC layer. Blocking operation.
 *
 * @param h Pointer to the MAC config.
 * @param data Pointer to a buffer to store received data.
 * @return Length of received message
 */
extern int (*MAC_recv)(MAC *h, unsigned char *data);

/**
 * @brief Receive data from the MAC layer with a timeout. Non-blocking operation.
 *
 * @param h Pointer to the MAC config.
 * @param data Pointer to a buffer to store received data.
 * @param timeout Maximum wait time (in milliseconds).
 * @return Length of received message on success, 0 on timeout.
 */
extern int (*MAC_timedRecv)(MAC *h, unsigned char *data, unsigned int timeout);

#endif // STRP_H