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
#include "SMRP/SMRP.h"
#include "ProtoMon/ProtoMon.h"

// Configuration flags

static LogLevel loglevel = INFO;

static uint8_t self;
static unsigned int sleepDuration;

static pthread_t recvT;
static pthread_t sendT;

static void *sendMsg_func(void *args);
static void *recvMsg_func(void *args);

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

	sleepDuration = 15000;
	printf("%s - Sleep duration: %d ms\n", timestamp(), sleepDuration);
	fflush(stdout);

	ProtoMon_Config config;
	config.vizIntervalS = 60;
	config.loglevel = INFO;
	config.sendIntervalS = 20;
	config.self = self;
	config.monitoredLayers = PROTOMON_LAYER_ALL;
	config.initialSendWaitS = 30;
	ProtoMon_init(config);

	SMRP_Config smrp;
	smrp.beaconIntervalS = 30;
	smrp.loglevel = INFO;
	smrp.nodeTimeoutS = 60;
	smrp.recvTimeoutMs = 1000;
	smrp.self = self;
	smrp.senseDurationS = 15;
	SMRP_init(smrp);

	if (self != ADDR_SINK)
	{
		Routing_Header header;
		if (pthread_create(&sendT, NULL, sendMsg_func, &header) != 0)
		{
			printf("Failed to create send thread");
			exit(EXIT_FAILURE);
		}
		pthread_join(sendT, NULL);
	}
	else
	{
		Routing_Header header;
		if (pthread_create(&recvT, NULL, recvMsg_func, &header) != 0)
		{
			printf("Failed to create receive thread");
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
			printf("%s - RX: %02d (%02d) src: %02d msg: %s total: %02d\n", timestamp(), header->prev, header->RSSI, header->src, buffer, ++total[header->src]);
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

		uint8_t dest_addr = Routing_getnextHop(self, ADDR_SINK, 3);

		if (Routing_sendMsg(dest_addr, buffer, sizeof(buffer)))
		{
			printf("%s - TX: %02d msg: %04d total: %02d\n", timestamp(), dest_addr, msg, total);
			fflush(stdout);
		}

		usleep((rand() % 100000) + (sleepDuration * 1000)); // Sleep sleepDuration + 100ms to avoid busy waiting
	}
	return NULL;
}
