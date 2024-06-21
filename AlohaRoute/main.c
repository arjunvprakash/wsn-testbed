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

static uint8_t debugFlag = 0;
static unsigned int recvTimeout = 2000;
static enum NetworkMode nwMode = MULTI_HOP;
static enum OperationMode opMode = DEDICATED;
static uint8_t broadCastEnabled = 1;

static uint8_t self;
static unsigned int sleepDuration;

static pthread_t recvT;
static pthread_t sendT;

void *recvT_func(void *args);
static void *sendT_func(void *args);
static void printOpMode(OperationMode mode);
static uint8_t getDestAddr(uint8_t self, OperationMode mode, int broadCastEnabled);
static MAC initMAC(uint8_t addr, uint8_t debug, unsigned int timeout);
static uint8_t isRXNode(uint8_t addr, OperationMode mode);
static void *handleRoutingSend(void *args);
static void *handleRoutingReceive(void *args);

int main(int argc, char *argv[])
{
	self = (uint8_t)atoi(argv[1]);
	printf("%s - Node: %02X\n", timestamp(), self);
	printf("%s - Network Mode: %s\n", timestamp(), (nwMode == SINGLE_HOP ? "SINGLE_HOP" : "MULTI_HOP"));
	srand(self * time(NULL));

	// sleepDuration = randInRange(MIN_SLEEP_TIME, MAX_SLEEP_TIME);
	sleepDuration = MIN_SLEEP_TIME + (rand() % (MAX_SLEEP_TIME - MIN_SLEEP_TIME + 1));
	printf("%s - Sleep duration: %d ms\n", timestamp(), sleepDuration);

	if (nwMode == SINGLE_HOP)
	{

		MAC mac;
		MAC_init(&mac, atoi(argv[1]));
		mac.debug = debugFlag;
		mac.recvTimeout = recvTimeout;

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
		routingInit(self, debugFlag, recvTimeout);
		RouteHeader header;

		if (self != ADDR_SINK)
		{
			if (pthread_create(&sendT, NULL, handleRoutingSend, &header) != 0)
			{
				printf("Failed to create send thread");
				exit(EXIT_FAILURE);
			}
		}
		else
		{
			if (pthread_create(&recvT, NULL, handleRoutingReceive, &header) != 0)
			{
				printf("Failed to create receive thread");
				exit(EXIT_FAILURE);
			}
		}
	}
	pthread_join(sendT, NULL);
	pthread_join(recvT, NULL);

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
		MAC_recv(mac, buffer);

		// MAC_timedrecv(mac, buffer, 2);

		printf("%s - RX: %02X RSSI: (%02d) msg: %s\n", timestamp(), mac->recvH.src_addr, mac->RSSI, buffer);
		fflush(stdout);
		// usleep(sleepDuration * 1000);
	}
	return NULL;
}

static void *sendT_func(void *args)
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
		usleep(sleepDuration * 1000);
	}
	return NULL;
}

static void *handleRoutingReceive(void *args)
{

	RouteHeader *header = (RouteHeader *)args;
	while (1)
	{
		unsigned char buffer[240];

		fflush(stdout);

		// blocking
		int msgLen = routingReceive(header, buffer);

		// int msgLen = routingTimedReceive(header, buffer, 2);
		if (msgLen > 0)
		{
			printf("%s - RRX: %02X numHops: %02d RSSI: (%02d) msg: %s\n", timestamp(), header->src, header->numHops, header->RSSI, buffer);
			fflush(stdout);
		}

		// usleep(sleepDuration * 1000);
	}
	return NULL;
}

static void *handleRoutingSend(void *args)
{
	RouteHeader *header = (RouteHeader *)args;
	while (1)
	{

		char buffer[5];
		int msg = randCode(4);
		sprintf(buffer, "%04d", msg);

		uint8_t dest_addr = ADDR_SINK;

		if (routingSend(dest_addr, buffer, sizeof(buffer)))
		{
			printf("%s - RTX: %02X msg: %04d\n", timestamp(), dest_addr, msg);
		}

		fflush(stdout);
		usleep(sleepDuration * 1000);
	}
	return NULL;
}

static void printOpMode(OperationMode mode)
{
	switch (mode)
	{
	case DEDICATED:
		printf("%s - Operation Mode: DEDICATED (%s)  Broadcast: %d\n", timestamp(), (isRXNode(self, mode) ? "RX" : "TX"), broadCastEnabled);
		break;
	case MIXED:
		printf("%s - Operation Mode: MIXED  Broadcast: %d\n", timestamp(), broadCastEnabled);
		break;
	default:
		printf("%s - Unknown Operation Mode\n", timestamp());
		break;
	}
}

static uint8_t getDestAddr(uint8_t addr, OperationMode mode, int broadcast)
{
	uint8_t dest_addr;
	if (broadcast && (rand() % 100) <= (100 / ((POOL_SIZE / 2) + 1)))
	{
		dest_addr = ADDR_BROADCAST;
	}
	else
	{
		do
		{
			dest_addr = NODE_POOL[rand() % POOL_SIZE];
		} while (dest_addr == addr || (mode == DEDICATED && !isRXNode(dest_addr, mode)));
	}
	return dest_addr;
}

static uint8_t isRXNode(uint8_t addr, OperationMode mode)
{
	return addr == ADDR_BROADCAST || (mode == DEDICATED && (addr % 2 == 0));
}

static MAC initMAC(uint8_t addr, uint8_t debug, unsigned int timeout)
{

	GPIO_init();
	MAC m;
	MAC_init(&m, addr);
	m.debug = debug;
	m.recvTimeout = timeout;

	return m;
}