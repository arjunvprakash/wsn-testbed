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
#include "STRP/STRP.h"
#include "ProtoMon/ProtoMon.h"

// Configuration flags

static LogLevel loglevel = INFO;

static t_addr self;
static unsigned int sleepDuration;

static pthread_t recvT;
static pthread_t sendT;

static void *sendMsg_func(void *args);
static void *recvMsg_func(void *args);

int main(int argc, char *argv[])
{
	self = (t_addr)atoi(argv[1]);
	logMessage(INFO, "Node: %02d\n", self);
	logMessage(INFO, "Role : %s\n", self == ADDR_SINK ? "SINK" : "NODE");
	if (self != ADDR_SINK)
	{
		logMessage(INFO, "ADDR_SINK : %02d\n", ADDR_SINK);
	}
	srand(self * time(NULL));

	sleepDuration = 60000;
	logMessage(INFO, "Sleep duration: %d ms\n", sleepDuration);
	fflush(stdout);

	ProtoMon_Config config;
	config.vizIntervalS = 240;
	config.loglevel = INFO;
	config.sendIntervalS = 180;
	config.sendDelayS = 60;
	config.self = self;
	config.monitoredLevels = PROTOMON_LEVEL_ALL;
	config.initialSendWaitS = 15 + self;
	ProtoMon_init(config);

	STRP_Config strp;
	strp.beaconIntervalS = 15;
	strp.loglevel = INFO;
	strp.nodeTimeoutS = 60;
	strp.self = self;
	strp.senseDurationS = 10;
	strp.strategy = CLOSEST;
	MAC mac;
	strp.mac = &mac;
	STRP_init(strp);
	mac.ambient = 0;

	if (self != ADDR_SINK)
	{
		Routing_Header header;
		if (pthread_create(&sendT, NULL, sendMsg_func, &header) != 0)
		{
			logMessage(ERROR, "Failed to create send thread\n");
			fflush(stdout);
			exit(EXIT_FAILURE);
		}
		pthread_join(sendT, NULL);
	}
	else
	{
		Routing_Header header;
		if (pthread_create(&recvT, NULL, recvMsg_func, &header) != 0)
		{
			logMessage(ERROR, "Failed to create receive thread\n");
			fflush(stdout);
			exit(EXIT_FAILURE);
		}
		pthread_join(recvT, NULL);
	}

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
		if (msgLen > 0)
		{
			logMessage(INFO, "RX: %02d (%02d) src: %02d msg: %s total: %02d\n", header->prev, header->RSSI, header->src, buffer, ++total[header->src]);
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

		uint8_t dest_addr = ADDR_SINK;

		if (Routing_sendMsg(dest_addr, buffer, sizeof(buffer)))
		{
			logMessage(INFO, "TX: %02d msg: %s total: %02d\n", dest_addr, buffer, total);
			fflush(stdout);
		}

		usleep((rand() % 100000) + (sleepDuration * 1000)); // Sleep sleepDuration + 100ms to avoid busy waiting
	}
	return NULL;
}
