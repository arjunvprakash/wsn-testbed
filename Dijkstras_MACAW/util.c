#include <time.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h> // printf
#include <stdarg.h> // va_list, va_start, va_end

#include "common.h"

// Returns the current timestamp. Sample: 2024-06-16T11:56:23
// Dependency: STRP, ProtoMon, main
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

// Returns a random value within the range
unsigned long randInRange(unsigned long min, unsigned long max)
{
    return min + (rand() % (max - min + 1));
}

// Returns a random code with n digits
int randCode(int n)
{
    int maxValue = (int)pow(10, n);
    return rand() % maxValue;
}

// Log a message to the console
// Parameters:
//  logLevel - The log level of the message.
//  format - The format string of the message.
//  ... - The arguments of the format string.
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
    fflush(stdout);
    va_end(args);
}
