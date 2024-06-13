#include <stdio.h> // printf
#include <stdint.h>
#include <signal.h>
#include <string.h>
#include <time.h>

#include "SX1262/SX1262.h"
#include "util.h"

/*typedef enum MSGType {
    MSG, ACK, RTS, CTS, wakeBEA, wakeACK, ERR
} MSGType;*/

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

uint8_t msg[240];

int main(int argc, char *argv[])
{
    signal(SIGINT, sigHandler);

    SX1262_init(868, SX1262_Transmission);

    // Slot slot[128];
    Slot slot;
    int count = 0;

    struct timespec start;
    // clock_gettime(CLOCK_REALTIME, &start);

    // while (!stop && count < sizeof(slot) / sizeof(slot[0])) {
    printf("%s - Sniffer Daemon Starting...\n", get_timestamp());
    while (1)
    {

        uint8_t ctrl;
        SX1262_recv(&ctrl, sizeof(ctrl));

        printf("%s - DEBUG: ctrl=%02X\n", get_timestamp(), ctrl);

        // clock_gettime(CLOCK_REALTIME, &slot.time);

        if (ctrl == '\xC4' || ctrl == '\xC8' || ctrl == '\xCE')
        {
            // slot.type = MSG;
            strcpy(slot.type, "MSG");

            uint8_t header[7];
            SX1262_recv(header, sizeof(header));
            slot.src_addr = header[0];
            slot.dst_addr = header[1];

            uint16_t msg_len;
            memcpy(&msg_len, &header[4], sizeof(msg_len));

            // uint8_t msg[msg_len];
            SX1262_recv(msg, sizeof(msg));
            msg[msg_len] = '\0';
        }
        else if (ctrl == '\xC5' || ctrl == '\xC9' || ctrl == '\xCF')
        {
            // slot.type = ACK;
            strcpy(slot.type, "ACK");

            uint8_t header[4];
            SX1262_recv(header, sizeof(header));
            slot.src_addr = header[0];
            slot.dst_addr = header[1];
        }
        else if (ctrl == '\xC6' || ctrl == '\xCC')
        {
            // slot.type = RTS;
            strcpy(slot.type, "RTS");

            uint8_t header[4];
            SX1262_recv(header, sizeof(header));
            slot.src_addr = header[0];
            slot.dst_addr = header[1];
        }
        else if (ctrl == '\xC7' || ctrl == '\xCD')
        {
            // slot.type = CTS;
            strcpy(slot.type, "CTS");

            uint8_t header[4];
            SX1262_recv(header, sizeof(header));
            slot.src_addr = header[0];
            slot.dst_addr = header[1];
        }
        else if (ctrl == '\xCA')
        {
            // slot.type = wakeBEA;
            strcpy(slot.type, "BEA");

            uint8_t header[2];
            SX1262_recv(header, sizeof(header));
            slot.src_addr = header[0];
            slot.dst_addr = header[1];
        }
        else if (ctrl == '\xCB')
        {
            // slot.type = wakeACK;
            strcpy(slot.type, "ACK");

            uint8_t header[2];
            SX1262_recv(header, sizeof(header));
            slot.src_addr = header[0];
            slot.dst_addr = header[1];
        }
        else if (ctrl == '\xC1' || ctrl == '\xC0' || ctrl == '\xC1' || ctrl == '\xC2' || ctrl == '\xC3' || ctrl == '\x00' || ctrl == '\x01')
        {
            uint8_t ambient[3];
            int8_t noise;
            SX1262_recv(ambient, sizeof(ambient));
            strcpy(slot.type, "AMB");
            if (ambient[0] == '\x00' && ambient[1] == '\x01')
            {
                noise = -ambient[2] / 2;
                printf("%s - Ambient noise : %02X RSSI:%02X", get_timestamp(), noise, ambient[2]);
            }
            else
            {
                // ungültiges Ambient Noise ausgeben
                printf("%s - Ambient-Noise Antwort ungültig: Anfang = %02X, Länge = %02X, RSSI = %hhd\n", get_timestamp(), ambient[0], ambient[1], ambient[2]);
            }
        }
        else
        {
            // slot.type = ERR;
            strcpy(slot.type, "ERR");

            // uint8_t c;
            // printf("%s - Discard buffer : ", get_timestamp());
            // while (SX1262_timedrecv(&c, sizeof(c), 100))
            // {
            //     printf("%02X ", c);
            // }
            // printf("\n");
        }

        int8_t RSSI;
        SX1262_recv(&RSSI, sizeof(RSSI));

        if (strcmp(slot.type, "MSG") == 0)
        {
            printf("%s - %s: pi%d -> pi%d %d [%s]\n", get_timestamp(), slot.type, slot.src_addr, slot.dst_addr, RSSI, msg);
        }
        else if (strcmp(slot.type, "AMB") != 0)
        {
            printf("%s - %s: pi%d -> pi%d %d\n", get_timestamp(), slot.type, slot.src_addr, slot.dst_addr, RSSI);
        }

        fflush(stdout);

        // count++;
    }

    // FILE *file = fopen("log.txt", "w");

    // fprintf(file, "      ");
    // for (int i = 0; i < count; i++)
    //     // fprintf(file, "| %3d.%3d  ", slot[i].time.tv_sec - start.tv_sec, slot[i].time.tv_nsec / 1000000);
    //     fprintf(file, "| %-7.3f  ", (slot[i].time.tv_sec - start.tv_sec) + slot[i].time.tv_nsec / 1e9);
    // fprintf(file, "\n");

    // for (uint8_t i = 0; i < 25; i++)
    // {
    //     fprintf(file, "pi%-2d: ", i);
    //     for (int j = 0; j < count; j++)
    //     {
    //         if (i == slot[j].src_addr)
    //             fprintf(file, "| %s->pi%d ", slot[j].type, slot[j].dst_addr);
    //         else
    //             fprintf(file, "|          ");
    //     }
    //     fprintf(file, "\n");
    // }

    // // fflush(stdout);
    // fclose(file);

    return 0;
}
