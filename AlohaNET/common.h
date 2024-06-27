#ifndef COMMON_H
#define COMMON_H

/*
Constants for subnet group
*/
#define ADDR_BROADCAST 0XFF
#define ADDR_SINK 0X01
#define POOL_SIZE 3
uint8_t NODE_POOL[POOL_SIZE] = {0x07, 0x08};

#define MIN_SLEEP_TIME 1000 // ms
#define MAX_SLEEP_TIME 3000 // ms

enum MODE
{
    DEDICATED,
    MIXED
};

#endif
