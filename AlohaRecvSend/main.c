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

unsigned int self;

static pthread_t recvT;
static pthread_t sendT;

void *recvT_func(void *args);
void *send_t(void *args);

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
	char buffer[18];
	sprintf(buffer, "Receive-Send Pi %d\n", self);

	// send buffer
	MAC_send(mac, ADDR_GATEWAY, buffer, sizeof(buffer));
	fflush(stdout);
}

void *recvT_func(void *args)
{
	MAC *mac = (MAC *)args;
	while (1)
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
		sleep(3);
	}
	return NULL;
}

void *sendT_func(void *args)
{
	MAC *mac = (MAC *)args;
	while (1)
	{
		char buffer[18];
		sprintf(buffer, "Hello from Pi %d\n", self);

		// send buffer
		MAC_send(mac, ADDR_GATEWAY, buffer, sizeof(buffer));
		fflush(stdout);
		sleep(3);
	}
	return NULL;
}

int main(int argc, char *argv[])
{
	GPIO_init();

	// ALOHA-Protokoll initialisieren
	MAC mac;

	self = atoi(argv[1]);

	printf("Self: %d\n", self);
	MAC_init(&mac, atoi(argv[1]));
	mac.debug = 1;
	mac.recvTimeout = 3000;

	// Threading implementation

	if (pthread_create(&recvT, NULL, recvT_func, &mac) != 0)
	{
		printf("Failed to create receive thread");
		exit(EXIT_FAILURE);
	}

	if (pthread_create(&sendT, NULL, sendT_func, &mac) != 0)
	{
		printf("Failed to create send thread");
		exit(EXIT_FAILURE);
	}

	pthread_join(recvT, NULL);
	pthread_join(sendT, NULL);

	return 0;

	// Basic implementation
	// while (1) {

	// 	receive(&mac);

	// printf("Go to sleep for 3 seconds...\n");
	// sleep(3);

	// 	send(&mac);

	// }
}
