#ifndef COMMON_H
#define COMMON_H

// Constants

/**
 * @brief Maximum # of nodes in the network
 */
#define MAX_ACTIVE_NODES 32

/**
 * @brief Broadcast address
 */
#define ADDR_BROADCAST 0XFF

/**
 * @brief Address of the sink/gateway node
 */
#define ADDR_SINK 0XE

/**
 * @brief Enum for log levels used in the logging system.
 */
typedef enum LogLevel
{
    INFO,  // Default log level for general information messages.
    DEBUG, // Log level for debugging messages, providing detailed information.
    TRACE, // Log level for tracing execution flow, useful for in-depth analysis.
    ERROR  // Log level for error messages indicating failures.
} LogLevel;

#endif
