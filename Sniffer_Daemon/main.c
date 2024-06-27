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
            ptr += 7;
            uint8_t *msg = (uint8_t *)malloc(len);
            memcpy(msg, ptr, len);
            printf("%s - [R] %02X->(%02d)[%02X->%02X]->%02X RSSI: (%02d) msg: %s\n", timestamp(), buffer[2], buffer[3], mac->recvH.src_addr, mac->recvH.dst_addr, buffer[1], mac->RSSI, msg);
            free(msg);
        }
        else if (ctrl == CTRL_MSG)
        {
            printf("%s - %02X -> %02X RSSI: (%02d) msg: %s\n", timestamp(), mac->recvH.src_addr, mac->recvH.dst_addr, mac->RSSI, buffer);
        }
        else if (ctrl == CTRL_BCN)
        {
            printf("%s - [R] Beacon src: %02X RSSI: (%02d) %s\n", timestamp(), mac->recvH.src_addr, mac->RSSI, msg);
        }
        // MAC_timedrecv(mac, buffer, 2);

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
    printf("%s - Node: %02X\n", timestamp(), self);
    printf("%s - Mode: Sniffer\n", timestamp());

    receiveT_func(&mac);

    return 0;
}
