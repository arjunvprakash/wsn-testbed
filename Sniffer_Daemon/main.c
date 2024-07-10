#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>

#include "ALOHA/ALOHA.h"
#include "GPIO/GPIO.h"
#include "common.h"
#include "util.h"

static uint8_t self;
static uint8_t msg[240];

typedef struct Beacon
{
    uint8_t ctrl;
    uint8_t parent;
} Beacon;

void *receiveT_func(void *args)
{
    MAC *mac = (MAC *)args;
    while (1)
    {
        unsigned char buffer[240];

        fflush(stdout);

        int len = MAC_recv(mac, buffer);
        uint8_t ctrl = *buffer;
        if (ctrl == CTRL_PKT)
        {
            uint8_t *ptr = buffer;
            ptr += 8;
            uint8_t *msg = (uint8_t *)malloc(len);
            memcpy(msg, ptr, len);
            printf("%s - [R] %02d(%02d)->%02d hops: %02d dst: %02d src: %02d parent: %02d msg: %s\n", timestamp(), mac->recvH.src_addr, mac->RSSI, mac->recvH.dst_addr, buffer[4], buffer[1], buffer[2], buffer[3], msg);
            free(msg);
        }
        else if (ctrl == CTRL_MSG)
        {
            printf("%s - %02d -> %02d RSSI: (%02d) msg: %s\n", timestamp(), mac->recvH.src_addr, mac->recvH.dst_addr, mac->RSSI, buffer);
        }
        else if (ctrl == CTRL_BCN)
        {
            printf("%s - [R] Beacon src: %02d (%02d) parent: %02d\n", timestamp(), mac->recvH.src_addr, mac->RSSI, buffer[1]);
        }

        fflush(stdout);
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    GPIO_init();

    MAC mac;
    MAC_init(&mac, atoi(argv[1]));
    // mac.debug = 1;
    mac.recvTimeout = 3000;

    self = (uint8_t)atoi(argv[1]);
    printf("%s - Node: %02d\n", timestamp(), self);
    printf("%s - Mode: Sniffer\n", timestamp());

    receiveT_func(&mac);

    return 0;
}
