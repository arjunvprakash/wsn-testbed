#ifndef ProtoMon_H
#define ProtoMon_H

#include <stdint.h>

#include "../common.h"
#include "../ALOHA/ALOHA.h"
#include "../STRP/STRP.h"

// Structs

typedef struct ProtoMon_Config
{
    uint8_t self;
    LogLevel loglevel;
    unsigned int routingTableIntervalS; // Interval to send routing table to sink
    unsigned int graphUpdateIntervalS;  // Interval to generate graph
    bool enableMonitoring;              // Enable monitoring
    uint8_t monitoredLayers;            // Bitmask for layers to monitor
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

// ####
int ProtoMon_Routing_sendMsg(uint8_t dest, uint8_t *data, unsigned int len);
int ProtoMon_Routing_recvMsg(Routing_Header *h, uint8_t *data);
int ProtoMon_Routing_timedRecvMsg(Routing_Header *header, uint8_t *data, unsigned int timeout);

extern int (*Routing_sendMsg)(uint8_t dest, uint8_t *data, unsigned int len);
extern int (*Routing_recvMsg)(Routing_Header *h, uint8_t *data);
extern int (*Routing_timedRecvMsg)(Routing_Header *h, uint8_t *data, unsigned int timeout);

void ProtoMon_setOrigRSend(int (*routing_func)(uint8_t, uint8_t *, unsigned int));
void ProtoMon_setOrigRRecv(int (*routing_func)(Routing_Header *, uint8_t *));
void ProtoMon_setOrigRTimedRecv(int (*routing_func)(Routing_Header *, uint8_t *, unsigned int));

int ProtoMon_MAC_send(MAC *h, unsigned char dest, unsigned char *data, unsigned int len);
int ProtoMon_MAC_recv(MAC *h, unsigned char *data);
int ProtoMon_MAC_timedRecv(MAC *h, unsigned char *data, unsigned int timeout);

extern int (*MAC_send)(MAC *h, unsigned char dest, unsigned char *data, unsigned int len);
extern int (*MAC_recv)(MAC *h, unsigned char *data);
extern int (*MAC_timedRecv)(MAC *h, unsigned char *data, unsigned int timeout);

void ProtoMon_setOrigMACSend(int (*routing_func)(MAC *, unsigned char, unsigned char *, unsigned int));
void ProtoMon_setOrigMACRecv(int (*routing_func)(MAC *, unsigned char *));
void ProtoMon_setOrigMACTimedRecv(int (*routing_func)(MAC *, unsigned char *, unsigned int));

#endif // STRP_H