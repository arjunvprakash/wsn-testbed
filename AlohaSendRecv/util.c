#include <time.h>
#include <string.h>

char* get_timestamp() {
    time_t current_time;
    struct tm* time_info;
    static char time_buffer[20]; // Static buffer to avoid memory allocation/deallocation

    time(&current_time);               // Get current time
    time_info = gmtime(&current_time); // Convert to UTC time

    // Format the timestamp
    strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%dT%H:%M:%S", time_info);

    return time_buffer;
}