#include <time.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>  // printf
#include <stdarg.h> // va_list, va_start, va_end

#include "common.h"

/**
 * @returns Current local timestamp in the yyyy-mm-dd'T'hh:mm:ss format. Eg: 2024-06-16T11:56:23
 */
char *timestamp()
{
    time_t current_time;
    struct tm *time_info;
    static char time_buffer[20];
    time(&current_time);                  // Get current time
    time_info = localtime(&current_time); // Convert to local time

    // Format the timestamp
    strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%dT%H:%M:%S", time_info);

    return time_buffer;
}

/**
 * @returns A random value within the range
 * @param min
 * @param max
 */
unsigned long randInRange(unsigned long min, unsigned long max)
{
    return min + (rand() % (max - min + 1));
}

/**
 * @returns A random value with the specified number of digits
 * @param n number of digits
 */
int randCode(int n)
{
    int maxValue = (int)pow(10, n);
    return rand() % maxValue;
}

/**
 * @brief Log a message to the console
 * @param logLevel - log level of the message.
 * @param format - format string of the message.
 * @param ... - args of the format string.
 */
void logMessage(LogLevel logLevel, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    // Print the log level specific information
    switch (logLevel)
    {
    case INFO:
        // Print the timestamp
        printf("%s - ", timestamp());
        break;
    case DEBUG:
        printf("# ");
        break;
    case TRACE:
        printf("## ");
        break;
    case ERROR:
        printf("### ");
        break;
    }

    vprintf(format, args);
    // fflush(stdout);
    va_end(args);
}
