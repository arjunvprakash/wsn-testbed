#ifndef MAC_H
#define MAC_H
#pragma once

#include <stdint.h>

#include "metric.h"

// To be able to be compatible with ProtoMon, the MAC protocol must implement these APIs

typedef struct MAC MAC;

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


/**
 * @returns size of the MAC header
 * @note Dependency with ProtoMon
 */
uint8_t MAC_getHeaderSize();

/**
 * @returns CSV header of metrics collected by MAC protocol.
 * @note Dependency with ProtoMon
 */
// uint8_t *MAC_getMetricsHeader();

/**
 * @returns Metric corresponding to the specified node collected by MAC protocol.
 * @note Dependency with ProtoMon
 */
Metric MAC_getMetricsDataV2(uint8_t addr);

#endif // MAC_H