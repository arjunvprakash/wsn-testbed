#ifndef UTIL_H
#define UTIL_H

#include "common.h"

char *timestamp();
int randCode(int n);
unsigned long randInRange(unsigned long min, unsigned long max);

#define _PRINT_TRACE_ printf("### Trace: - %s:%d\n", __FILE__, __LINE__);


#endif