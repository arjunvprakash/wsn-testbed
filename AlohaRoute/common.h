#ifndef COMMON_H
#define COMMON_H

/*
Constants for subnet group
*/

#define CTRL_PKT '\x45' // Routing layer packet

#define ADDR_BROADCAST 0XFF
#define ADDR_SINK 0X01
#define POOL_SIZE 3
extern uint8_t NODE_POOL[POOL_SIZE];

#define MIN_SLEEP_TIME 1000 // ms
#define MAX_SLEEP_TIME 3000 // ms

/*
Communication Type : Routing
*/
typedef enum NetworkMode
{
    ROUTING
} NetworkMode;

#define _PRINT_TRACE_ printf("### Trace: - %s:%d", __FILE__, __LINE__);

#endif
