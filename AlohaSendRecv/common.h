#ifndef COMMON_H
#define COMMON_H

#define ADDR_Gateway '\x07'

/*
Constants for subnet group
*/
#define GROUP_SIZE 7
#define ADDR_BROADCAST 0XFF
uint8_t ADDR_GROUP[GROUP_SIZE] = {0x04, 0x05, 0x06, 0x07, 0x08, 0x09, ADDR_BROADCAST}; // 0XFF for broadcast

#define MIN_SLEEP_TIME 1000 // ms
#define MAX_SLEEP_TIME 3000 // ms

enum MODE
{
    DEDICATED,
    MIXED
};

#endif
