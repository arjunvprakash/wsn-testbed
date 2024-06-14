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

uint8_t self;
unsigned int sleepDuration;

static pthread_t recvT;
static pthread_t sendT;

void *recvT_func(void *args);
void *sendT_func(void *args);
int getMsg();
uint8_t getNewDest();
void node_init(char *argv[]);
void print_mode();

static enum MODE mode = MIXED;

uint8_t isGateway(uint8_t addr)
{
	return addr == ADDR_BROADCAST || (mode == DEDICATED && (addr % 2 == 0));
}

void receive(MAC *mac)
{
	// Puffer für größtmögliche Nachricht
	unsigned char buffer[240];

	printf("Waiting for message...\n");
	fflush(stdout);

	// Blockieren bis eine Nachricht ankommt
	// MAC_recv(mac, buffer);

	MAC_timedrecv(mac, buffer, 2);

	printf("Received : %s\n", buffer);
	fflush(stdout);
}

void send(MAC *mac)
{
	char buffer[7];
	sprintf(buffer, "Pi %02X\n", self);

	// send buffer
	MAC_send(mac, ADDR_Gateway, buffer, sizeof(buffer));
	fflush(stdout);
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

		printf("%s - RX: %02X RSSI: (%02d) msg: %s\n", get_timestamp(), mac->recvH.src_addr, mac->RSSI, buffer);
		fflush(stdout);
		// usleep(sleepDuration * 10000);
	}
	return NULL;
}

void *sendT_func(void *args)
{
	MAC *mac = (MAC *)args;
	while (1)
	{
		char buffer[5];
		int msg = getMsg();
		sprintf(buffer, "%04d", msg);

		uint8_t dest_addr = getNewDest();

		// send buffer
		if (MAC_send(mac, dest_addr, buffer, sizeof(buffer)))
		{
			printf("%s - TX: %02X msg: %04d\n", get_timestamp(), dest_addr, msg);
		}
		fflush(stdout);
		usleep(sleepDuration * 10000);
	}
	return NULL;
}

int getMsg()
{
	return rand() % 10000;
}

int main(int argc, char *argv[])
{
	GPIO_init();

	// ALOHA-Protokoll initialisieren
	MAC mac;
	MAC_init(&mac, atoi(argv[1]));
	// mac.debug = 1;
	mac.recvTimeout = 3000;

	node_init(argv);
	printf("%s - Node: %02X\n", get_timestamp(), self);
	printf("%s - sleep duration: %d ms\n", get_timestamp(), sleepDuration);
	print_mode();

	// Threading implementation

	if (mode == MIXED || (mode == DEDICATED && isGateway(self)))
	{
		if (pthread_create(&recvT, NULL, receiveT_func, &mac) != 0)
		{
			printf("Failed to create receive thread");
			exit(EXIT_FAILURE);
		}
	}

	if (mode == MIXED || (mode == DEDICATED && !isGateway(self)))
	{
		if (pthread_create(&sendT, NULL, sendT_func, &mac) != 0)
		{
			printf("Failed to create send thread");
			exit(EXIT_FAILURE);
		}
	}

	pthread_join(recvT, NULL);
	pthread_join(sendT, NULL);

	return 0;

	// Normal implemenation
	// while (1) {

	// 	send(&mac);

	// 	printf("Go to sleep for 3 seconds...\n");
	//     	sleep(3);

	// 	receive(&mac);

	// }
}

void node_init(char *argv[])
{
	self = (uint8_t)atoi(argv[1]);
	sleepDuration = MIN_SLEEP_TIME + (rand() % (MAX_SLEEP_TIME - MIN_SLEEP_TIME + 1));
}

uint8_t getNewDest()
{
	uint8_t dest_addr;
	do
	{
		dest_addr = ADDR_GROUP[rand() % GROUP_SIZE];
	} while (dest_addr == self || (mode == DEDICATED && !isGateway(dest_addr)));
	return dest_addr;
}

void print_mode()
{
	switch (mode)
	{
	case DEDICATED:
		printf("%s - Mode: DEDICATED (%s)\n", get_timestamp(), (isGateway(self) ? "RX" : "TX"));
		break;
	case MIXED:
		printf("%s - Mode: MIXED\n", get_timestamp());
		break;
	default:
		printf("%s - Unknown mode\n", get_timestamp());
		break;
	}
}