#ifndef COMMON_H
#define COMMON_H

#define ADDR_Gateway '\x07'

/*
Constants for subnet group
*/
#define GROUP_SIZE 5
// uint8_t ADDR_GROUP[GROUP_SIZE] = {0x06, 0x07, 0x08, 0x15, 0x16};

uint8_t ADDR_GROUP[GROUP_SIZE] = {0x06};
#define MIN_SLEEP_TIME 1000 // ms
#define MAX_SLEEP_TIME 3000 // ms

#endif
