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

// Operation mode of all nodes in the P2P network mode
typedef enum OperationMode
{
    DEDICATED,
    MIXED
} OperationMode;

/*
Communication Type : P2P, Routing
*/
typedef enum NetworkMode
{
    SINGLE_HOP,
    MULTI_HOP
} NetworkMode;

#define BLURT(buffer) printf("TRACE: %s:%d:%s -> %s"__FILE__, __LINE__, __func__, buffer)

#endif
