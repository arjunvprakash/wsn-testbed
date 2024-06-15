#ifndef COMMON_H
#define COMMON_H

/*
Constants for subnet group
*/
#define GROUP_SIZE 7
#define ADDR_BROADCAST 0XFF
#define ADDR_SINK 0X01
#define POOL_SIZE 7
uint8_t ADDR_POOL[POOL_SIZE] = {0x04, 0x05, 0x06, 0x07, 0x08, 0x09, ADDR_BROADCAST}; // 0XFF for broadcast

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
typedef enum NetworkMode {
    SINGLE_HOP,
    MULTI_HOP
} NetworkMode;


#endif
