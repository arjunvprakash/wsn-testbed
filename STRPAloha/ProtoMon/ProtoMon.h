#ifndef ProtoMon_H
#define ProtoMon_H

#include <stdint.h>

#include "../common.h"
#include "../ALOHA/ALOHA.h"
#include "../STRP/STRP.h"

// Structs

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

typedef enum
{
    PROTOMON_LAYER_NONE = 0x00,    // No monitoring
    PROTOMON_LAYER_MAC = 0x01,     // Monitor MAC
    PROTOMON_LAYER_ROUTING = 0x02, // Monitor Routing
    PROTOMON_LAYER_ALL = 0xFF      // Monitor all layers
} ProtoMon_Layer;

// Function Declarations
void ProtoMon_init(ProtoMon_Config config);

extern int (*Routing_sendMsg)(uint8_t dest, uint8_t *data, unsigned int len);
extern int (*Routing_recvMsg)(Routing_Header *h, uint8_t *data);
extern int (*Routing_timedRecvMsg)(Routing_Header *h, uint8_t *data, unsigned int timeout);

extern int (*MAC_send)(MAC *h, unsigned char dest, unsigned char *data, unsigned int len);
extern int (*MAC_recv)(MAC *h, unsigned char *data);
extern int (*MAC_timedRecv)(MAC *h, unsigned char *data, unsigned int timeout);

#endif // STRP_H