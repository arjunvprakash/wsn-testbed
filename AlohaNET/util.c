#include <time.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <stdlib.h>

#include "common.h"
#include "ALOHA/ALOHA.h"
#include "GPIO/GPIO.h"

char *get_timestamp()
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

uint8_t isRX(uint8_t addr, OperationMode opMode)
{
    return addr == ADDR_BROADCAST || (opMode == DEDICATED && (addr % 2 == 0));
}

MAC initNode(uint8_t addr, unsigned short debug)
{
    GPIO_init();
    MAC mac;
    MAC_init(&mac, addr);
    mac.debug = debug;
    mac.recvTimeout = 3000;

    return mac;
}

uint8_t getDestAddr(uint8_t self, OperationMode opMode)
{
    uint8_t dest_addr;
    do
    {
        dest_addr = ADDR_POOL[rand() % POOL_SIZE];
    } while (dest_addr == self || (opMode == DEDICATED && !isRX(dest_addr, opMode)));
    return dest_addr;
}

int getSleepDur()
{
    return MIN_SLEEP_TIME + (rand() % (MAX_SLEEP_TIME - MIN_SLEEP_TIME + 1));
}

int getMsg()
{
    return rand() % 10000;
}

uint8_t getNextHopAddr(uint8_t self)
{
    uint8_t addr;
    do
    {
        addr = ADDR_POOL[rand() % POOL_SIZE];
    } while (addr >= self);
    return addr;
}