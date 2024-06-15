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

// Configuration flags

static enum NetworkMode nwMode = SINGLE_HOP;
static enum OperationMode opMode = MIXED;
static unsigned short debug = 0;

uint8_t self;
unsigned int sleepDuration;

static pthread_t recvT;
static pthread_t sendT;

void *recvT_func(void *args);
void *sendT_func(void *args);
int getMsg();
int getSleepDur();
void printOpMode(OperationMode opMode);

void *receiveT_func(void *args)
{
	MAC *mac = (MAC *)args;
	while (1)
	{
		unsigned char buffer[240];

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

		uint8_t dest_addr = getDestAddr(self, opMode);

		if (MAC_send(mac, dest_addr, buffer, sizeof(buffer)))
		{
			printf("%s - TX: %02X msg: %04d\n", get_timestamp(), dest_addr, msg);
		}
		fflush(stdout);
		usleep(sleepDuration * 10000);
	}
	return NULL;
}

int main(int argc, char *argv[])

{

	self = (uint8_t)atoi(argv[1]);
	printf("%s - Node: %02X\n", get_timestamp(), self);

	if (nwMode == SINGLE_HOP)
	{
		MAC mac = initNode(self, debug);

		sleepDuration = getSleepDur();
		printf("%s - Sleep duration: %d ms\n", get_timestamp(), sleepDuration);
		printOpMode(opMode);

		if (opMode == MIXED || (opMode == DEDICATED && isRX(self, opMode)))
		{
			if (pthread_create(&recvT, NULL, receiveT_func, &mac) != 0)
			{
				printf("Failed to create receive thread");
				exit(EXIT_FAILURE);
			}
		}

		if (opMode == MIXED || (opMode == DEDICATED && !isRX(self, opMode)))
		{
			if (pthread_create(&sendT, NULL, sendT_func, &mac) != 0)
			{
				printf("Failed to create send thread");
				exit(EXIT_FAILURE);
			}
		}
	}
	else if (nwMode == MULTI_HOP)
	{
	}
	pthread_join(recvT, NULL);
	pthread_join(sendT, NULL);

	return 0;
}

void printOpMode(OperationMode opMode)
{
	switch (opMode)
	{
	case DEDICATED:
		printf("%s - Operation Mode: DEDICATED (%s)\n", get_timestamp(), (isRX(self, opMode) ? "RX" : "TX"));
		break;
	case MIXED:
		printf("%s - Operation Mode: MIXED\n", get_timestamp());
		break;
	default:
		printf("%s - Unknown Operation Mode\n", get_timestamp());
		break;
	}
}