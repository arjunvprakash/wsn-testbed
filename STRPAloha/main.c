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
#include "ProtoMon/ProtoMon.h"

// Configuration flags

static LogLevel loglevel = INFO;
static unsigned int recvTimeout = 3000;

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

	sleepDuration = 2000;
	printf("%s - Sleep duration: %d ms\n", timestamp(), sleepDuration);
	fflush(stdout);
	STRP_Config strp;
	strp.beaconIntervalS = 5;
	strp.loglevel = INFO;
	strp.nodeTimeoutS = 10;
	strp.recvTimeoutMs = 3000;
	strp.self = self;
	strp.senseDurationS = 15;
	strp.strategy = CLOSEST_LOWER;
	STRP_init(strp);

	ProtoMon_setOrigRSendMsg(STRP_sendMsg);
	ProtoMon_setOrigRRecvMsg(STRP_recvMsg);
	ProtoMon_setOrigRTimedRecvMsg(STRP_timedRecvMsg);
	ProtoMon_setOrigMACSendMsg(ALOHA_send);
	ProtoMon_setOrigMACRecvMsg(ALOHA_recv);
	ProtoMon_setOrigMACTimedRecvMsg(ALOHA_timedrecv);
	ProtoMon_Config config;
	config.graphUpdateIntervalS = 30;
	config.loglevel = INFO;
	config.routingTableIntervalS = 15;
	config.self = self;
	ProtoMon_init(config);

	Routing_Header header;
	if (self != ADDR_SINK)
	{
		if (pthread_create(&sendT, NULL, sendMsg_func, &header) != 0)
		{
			printf("Failed to create send thread");
			exit(EXIT_FAILURE);
		}
	}
	else
	{
		if (pthread_create(&recvT, NULL, recvMsg_func, &header) != 0)
		{
			printf("Failed to create receive thread");
			exit(EXIT_FAILURE);
		}
	}

	pthread_join(sendT, NULL);
	pthread_join(recvT, NULL);

	return 0;
}

static void *recvMsg_func(void *args)
{
	unsigned int total[25] = {0};
	Routing_Header *header = (Routing_Header *)args;
	while (1)
	{
		unsigned char buffer[240];

		fflush(stdout);

		int msgLen = Routing_timedRecvMsg(header, buffer, 1);
		if (msgLen > 0)
		{
			printf("%s - RX: %02d (%02d) src: %02d hops: %02d msg: %s total: %02d\n", timestamp(), header->prev, header->RSSI, header->src, header->numHops, buffer, ++total[header->src]);
			fflush(stdout);
		}
		usleep(rand() % 1000);
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
			printf("%s - TX: %02d msg: %04d total: %02d\n", timestamp(), dest_addr, msg, total);
			fflush(stdout);
		}

		usleep(sleepDuration * 1000);
	}
	return NULL;
}
