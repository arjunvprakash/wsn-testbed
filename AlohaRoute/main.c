#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>

#include "ALOHA/ALOHA.h"
#include "GPIO/GPIO.h"
#include "common.h"
#include "util.h"
#include "Routing/routing.h"

// Configuration flags

static uint8_t debug = 0;
static unsigned int recvTimeout = 3000;
static enum NetworkMode nwMode = SINGLE_HOP;
static enum OperationMode opMode = DEDICATED;
static uint8_t broadCastEnabled = 1;

static uint8_t self;
static unsigned int sleepDuration;

static pthread_t recvT;
static pthread_t sendT;

void *recvT_func(void *args);
void *sendT_func(void *args);
void printOpMode(OperationMode opMode);
uint8_t getDestAddr(uint8_t self, OperationMode opMode, int broadCastEnabled);
static MAC initMAC(uint8_t addr, uint8_t debug, unsigned int timeout);
uint8_t isRXNode(uint8_t addr, OperationMode opMode);
void *handleRoutingSend(void *args);
void *handleRoutingReceive(void *args);

int main(int argc, char *argv[])
{
	self = (uint8_t)atoi(argv[1]);
	printf("%s - Node: %02X\n", timestamp(), self);
	printf("%s - Network Mode: %s\n", timestamp(), (nwMode == SINGLE_HOP ? "SINGLE_HOP" : "MULTI_HOP"));

	sleepDuration = randInRange(MIN_SLEEP_TIME, MAX_SLEEP_TIME);
	printf("%s - Sleep duration: %d ms\n", timestamp(), sleepDuration);

	if (nwMode == SINGLE_HOP)
	{
		MAC mac = initMAC(self, debug, recvTimeout);

		printOpMode(opMode);

		if (opMode == MIXED || (opMode == DEDICATED && isRXNode(self, opMode)))
		{
			if (pthread_create(&recvT, NULL, recvT_func, &mac) != 0)
			{
				printf("Failed to create receive thread");
				exit(EXIT_FAILURE);
			}
		}

		if (opMode == MIXED || (opMode == DEDICATED && !isRXNode(self, opMode)))
		{
			if (pthread_create(&sendT, NULL, sendT_func, &mac) != 0)
			{
				printf("Failed to create send thread");
				exit(1);
			}
		}
	}
	else if (nwMode == MULTI_HOP)
	{
		routingInit(self, debug, recvTimeout);
		printf("%s - ### Done routingInit\n", timestamp());
		RouteHeader header;

		if (self != ADDR_SINK)
		{
			if (pthread_create(&sendT, NULL, handleRoutingSend, &header) != 0)
			{
				printf("Failed to create send thread");
				exit(1);
			}
			printf("%s - ### sendT created\n", timestamp());
		}
		if (pthread_create(&recvT, NULL, handleRoutingReceive, &header) != 0)
		{
			printf("Failed to create receive thread");
			exit(EXIT_FAILURE);
		}
		printf("%s - ### recvT created\n", timestamp());
	}
	pthread_join(recvT, NULL);
	printf("%s - ### recvT join\n", timestamp());

	pthread_join(sendT, NULL);
	printf("%s - ### sendT join\n", timestamp());

	return 0;
}

void *recvT_func(void *args)
{
	MAC *mac = (MAC *)args;
	while (1)
	{
		unsigned char buffer[240];

		fflush(stdout);

		// blocking
		// MAC_recv(mac, buffer);

		MAC_timedrecv(mac, buffer, 2);

		printf("%s - RX: %02X RSSI: (%02d) msg: %s\n", timestamp(), mac->recvH.src_addr, mac->RSSI, buffer);
		fflush(stdout);
		// usleep(sleepDuration * 10000);
	}
	return NULL;
}

void *handleRoutingReceive(void *args)
{
	printf("%s - ### Inside handleRoutingReceive\n", timestamp());

	RouteHeader *header = (RouteHeader *)args;
	while (1)
	{
		printf("%s - ### Loop handleRoutingReceive\n", timestamp());
		unsigned char buffer[240];

		fflush(stdout);

		// blocking
		// routingReceive(header, buffer);

		routingTimedReceive(header, buffer, 2);

		printf("%s - RX: %02X numHops: %02d RSSI: (%02d) msg: %s\n", timestamp(), header->src, header->numHops, header->RSSI, buffer);
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
		int msg = randCode(4);
		sprintf(buffer, "%04d", msg);

		uint8_t dest_addr = getDestAddr(self, opMode, broadCastEnabled);

		if (MAC_send(mac, dest_addr, buffer, sizeof(buffer)))
		{
			printf("%s - TX: %02X msg: %04d\n", timestamp(), dest_addr, msg);
		}
		fflush(stdout);
		usleep(sleepDuration * 10000);
	}
	return NULL;
}

void *handleRoutingSend(void *args)
{
	printf("%s - ### Inside handleRoutingSend\n", timestamp());

	RouteHeader *header = (RouteHeader *)args;
	while (1)
	{
		printf("%s - ### Loop handleRoutingSend\n", timestamp());

		char buffer[5];
		int msg = randCode(4);
		sprintf(buffer, "%04d", msg);

		uint8_t dest_addr = ADDR_SINK;

		if (routingSend(dest_addr, buffer, sizeof(buffer)))
		{
			printf("%s - TX: %02X msg: %04d\n", timestamp(), dest_addr, msg);
		}
		fflush(stdout);
		usleep(sleepDuration * 10000);
	}
	return NULL;
}

void printOpMode(OperationMode opMode)
{
	switch (opMode)
	{
	case DEDICATED:
		printf("%s - Operation Mode: DEDICATED (%s)\n", timestamp(), (isRXNode(self, opMode) ? "RX" : "TX"));
		break;
	case MIXED:
		printf("%s - Operation Mode: MIXED\n", timestamp());
		break;
	default:
		printf("%s - Unknown Operation Mode\n", timestamp());
		break;
	}
}

uint8_t getDestAddr(uint8_t self, OperationMode opMode, int broadCastEnabled)
{
	uint8_t dest_addr;
	if (broadCastEnabled && (rand() % 100) < 20)
	{
		dest_addr = ADDR_BROADCAST;
	}
	else
	{
		do
		{
			dest_addr = NODE_POOL[rand() % POOL_SIZE];
		} while (dest_addr == self || (opMode == DEDICATED && !isRXNode(dest_addr, opMode)));
		return dest_addr;
	}
}

uint8_t isRXNode(uint8_t addr, OperationMode opMode)
{
	return addr == ADDR_BROADCAST || (opMode == DEDICATED && (addr % 2 == 0));
}

static MAC initMAC(uint8_t addr, uint8_t debug, unsigned int timeout)
{
	GPIO_init();
	MAC mac;
	MAC_init(&mac, addr);
	mac.debug = debug;
	mac.recvTimeout = timeout;

	return mac;
}