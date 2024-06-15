#ifndef UTIL_H
#define UTIL_H

#include "common.h"

char *get_timestamp();
uint8_t isRX(uint8_t addr, OperationMode opMode);
MAC initNode(uint8_t addr, unsigned short debug);
uint8_t getDestAddr(uint8_t self, OperationMode opMode);
int getSleepDur();
int getMsg();
uint8_t getNextHopAddr(uint8_t self);

#endif