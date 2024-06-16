#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include "ALOHA/ALOHA.h"
#include "GPIO/GPIO.h"
#include "common.h"
#include "util.h"

/*typedef enum MSGType {
    MSG, ACK, RTS, CTS, wakeBEA, wakeACK, ERR
} MSGType;*/

uint8_t self;

typedef struct Slot
{
    struct timespec time;
    uint8_t src_addr, dst_addr;
    // MSGType type;
    char type[4];
} Slot;

int stop = 0;
void sigHandler(int sig)
{
    stop = 1;
}

void *receiveT_func(void *args)
{
    MAC *mac = (MAC *)args;
    while (1)
    {
        // Puffer für größtmögliche Nachricht
        unsigned char buffer[240];

        // printf("Waiting for message...\n");
        fflush(stdout);

        // Blockieren bis eine Nachricht ankommt
        MAC_recv(mac, buffer);

        // MAC_timedrecv(mac, buffer, 2);

        printf("%s - %02X -> %02X RSSI: (%02d) msg: %s\n", timestamp(), mac->recvH.src_addr, mac->recvH.dst_addr, mac->RSSI, buffer);
        fflush(stdout);
        // usleep(sleepDuration * 10000);
    }
    return NULL;
}

uint8_t msg[240];

int main(int argc, char *argv[])
{
    GPIO_init();

    // ALOHA-Protokoll initialisieren
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
