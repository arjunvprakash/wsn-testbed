#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>

#include "common.h"
#include "util.h"
#include "Dijkstra/Dijkstra.h"
#include "ProtoMon/ProtoMon.h"

// Configuration flags

static LogLevel loglevel = INFO;

static uint8_t self;
static unsigned int sleepDuration;

static pthread_t recvT;
static pthread_t sendT;

static void *sendMsg_func(void *args);
static void *recvMsg_func(void *args);

static uint8_t nodes[] = {7, 8, 15};
int pool_size = (sizeof(nodes) / sizeof(nodes[0]));
static uint8_t dest[5];

void destinations(uint8_t self)
{
	uint8_t dst[pool_size - 1];
	int p = 0;
	for (int i = 0; i < pool_size; i++)
	{
		if (nodes[i] != self)
		{
			dest[p++] = nodes[i];
		}
	}
}

int main(int argc, char *argv[])
{
	self = (uint8_t)atoi(argv[1]);
	logMessage(INFO, "Node: %02d\n", self);
	logMessage(INFO, "Role : %s\n", self == ADDR_SINK ? "SINK" : "NODE");
	if (self != ADDR_SINK)
	{
		logMessage(INFO, "ADDR_SINK : %02d\n", ADDR_SINK);
	}
	srand(self * time(NULL));

	sleepDuration = 15000;
	logMessage(INFO, "Sleep duration: %d ms\n", sleepDuration);
	fflush(stdout);

	destinations(self);

	ProtoMon_Config config;
	config.vizIntervalS = 60;
	config.loglevel = INFO;
	config.sendIntervalS = 20;
	config.self = self;
	config.monitoredLayers = PROTOMON_LAYER_ROUTING | PROTOMON_LAYER_MAC;
	config.initialSendWaitS = 30;
	ProtoMon_init(config);

	Routing routing;
	Dijkstras_init(&routing, self);
	// routing.debug = 1;

	// if (self != ADDR_SINK)
	{
		Routing_Header header;
		if (pthread_create(&sendT, NULL, sendMsg_func, &header) != 0)
		{
			logMessage(ERROR, "Failed to create send thread\n");
			fflush(stdout);
			exit(EXIT_FAILURE);
		}
	}
	// else
	{
		Routing_Header header;
		if (pthread_create(&recvT, NULL, recvMsg_func, &header) != 0)
		{
			logMessage(ERROR, "Failed to create receive thread\n");
			fflush(stdout);
			exit(EXIT_FAILURE);
		}
	}
	pthread_join(recvT, NULL);
	pthread_join(sendT, NULL);

	return 0;
}

static void *recvMsg_func(void *args)
{
	unsigned int total[MAX_ACTIVE_NODES] = {0};
	Routing_Header *header = (Routing_Header *)args;
	while (1)
	{
		unsigned char buffer[240];

		fflush(stdout);

		int msgLen = Routing_timedRecvMsg(header, buffer, 1);
		// int msgLen = Dijkstras_timedrecv(header, buffer, 1);
		if (msgLen > 0)
		{
			logMessage(INFO, "RX: %02d msg: %s total: %02d\n", header->src, buffer, ++total[header->src]);
			fflush(stdout);
		}
		usleep((rand() % 100000) + 1000000); // Sleep 1-1.2s to prevent busy waiting
	}
	return NULL;
}

static void *sendMsg_func(void *args)
{
	Routing_Header *header = (Routing_Header *)args;
	unsigned int total;
	while (1)
	{
		char buffer[5];
		// int msg = randCode(4);
		int msg = (self * 1000) + (++total % 1000);
		sprintf(buffer, "%04d", msg);

		uint8_t dest_addr = dest[randInRange(0, pool_size - 2)];
		int r = Routing_sendMsg(dest_addr, buffer, strlen(buffer));
		// int r = Dijkstras_send(dest_addr, buffer, sizeof(buffer));
		if (r)
		{
			logMessage(INFO, "TX: %02d msg: %s total: %02d\n", dest_addr, buffer, total);
			fflush(stdout);
		}

		if (self == ADDR_SINK)
		{
			usleep((rand() % 100000) + (sleepDuration * 1000 * 2)); // Sleep 2 * sleepDuration + 100ms
		}
		else
		{
			usleep((rand() % 100000) + (sleepDuration * 1000)); // Sleep sleepDuration + 100ms to avoid busy waiting
		}
	}
	return NULL;
}
