#include <time.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#include "common.h"
#include "ALOHA/ALOHA.h"
#include "GPIO/GPIO.h"

// Returns the current timestamp. Sample: 2024-06-16T11:56:23
char *timestamp()
{
    time_t current_time;
    struct tm *time_info;
    static char time_buffer[20];
    time(&current_time);               // Get current time
    time_info = gmtime(&current_time); // Convert to UTC time

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
