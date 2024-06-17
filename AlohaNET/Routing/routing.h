#ifndef ROUTING_h
#define ROUTING_H

#include <stdint.h>

typedef struct RouteHeader
{
    uint8_t src;
    uint8_t dst;
    uint8_t prev;
    int RSSI;
    unsigned int numHops;
} RouteHeader;

int routingInit(uint8_t self, uint8_t debug, unsigned int timeout);
int routingSend(uint8_t dest, uint8_t *data, unsigned int len);
int routingReceive(RouteHeader *header, uint8_t *data);

#endif