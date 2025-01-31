#ifndef COMMON_H
#define COMMON_H

/*
Constants for subnet group
*/

// Constants
#define MAX_ACTIVE_NODES 32

#define ADDR_BROADCAST 0XFF
#define ADDR_SINK 0XE

#define MIN_SLEEP_TIME 1000 // ms
#define MAX_SLEEP_TIME 3000 // ms

#define _PRINT_TRACE_ printf("### Trace: - %s:%d\n", __FILE__, __LINE__);

typedef enum LogLevel
{
    INFO,
    DEBUG,
    TRACE
} LogLevel;

#endif
