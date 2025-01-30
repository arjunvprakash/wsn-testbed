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
#include "STRP/STRP.h"

// Configuration flags

static LogLevel loglevel = INFO;
static unsigned int recvTimeout = 3000;
static enum NetworkMode nwMode = ROUTING;

static uint8_t self;
static unsigned int sleepDuration;

static pthread_t recvT;
static pthread_t sendT;

static void *handleRoutingSend(void *args);
static void *handleRoutingReceive(void *args);

int main(int argc, char *argv[])
{
	self = (uint8_t)atoi(argv[1]);
	printf("%s - Node: %02d\n", timestamp(), self);
	printf("%s - Role : %s\n", timestamp(), self == ADDR_SINK ? "SINK" : "NODE");
	if (self != ADDR_SINK)
	{
		printf("%s - ADDR_SINK : %02d\n", timestamp(), ADDR_SINK);
	}
	srand(self * time(NULL));
	fflush(stdout);

	if (nwMode == ROUTING)
	{
		sleepDuration = 20000;
		printf("%s - Sleep duration: %d ms\n", timestamp(), sleepDuration);
		fflush(stdout);
		routingInit(self, loglevel, recvTimeout);
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

static void *handleRoutingReceive(void *args)
{
	unsigned int total[25] = {0};
	RouteHeader *header = (RouteHeader *)args;
	while (1)
	{
		unsigned char buffer[240];

		fflush(stdout);

		// blocking
		// int msgLen = routingReceive(header, buffer);

		int msgLen = routingTimedReceive(header, buffer, 1);
		if (msgLen > 0)
		{
			printf("%s - RX: %02d (%02d) src: %02d hops: %02d msg: %s total: %02d\n", timestamp(), header->prev, header->RSSI, header->src, header->numHops, buffer, ++total[header->src]);
			fflush(stdout);
		}
		usleep(rand() % 1000);
		// usleep(sleepDuration * 1000);
	}
	return NULL;
}

static void *handleRoutingSend(void *args)
{
	RouteHeader *header = (RouteHeader *)args;
	unsigned int total;
	while (1)
	{
		char buffer[5];
		// int msg = randCode(4);
		int msg = (self * 1000) + (++total % 1000);
		sprintf(buffer, "%04d", msg);

		uint8_t dest_addr = ADDR_SINK;

		if (routingSend(dest_addr, buffer, sizeof(buffer)))
		{
			printf("%s - TX: %02d msg: %04d total: %02d\n", timestamp(), dest_addr, msg, total);
			fflush(stdout);
		}

		usleep(sleepDuration * 1000);
	}
	return NULL;
}
