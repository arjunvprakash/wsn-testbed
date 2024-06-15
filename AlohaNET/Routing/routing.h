#ifndef ROUTING_h
#define ROUTING_H

#include <stdint.h>

typedef struct RouteHeader
{
    uint8_t src;
    uint8_t dst;
    int RSSI;
} RouteHeader;

int routing_init(uint8_t self, unsigned short debug);
int routing_send(uint8_t addr, unsigned char *msg, unsigned int len);
int routing_recv(unsigned char *msg, RouteHeader *header);

#endif