#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>

#include "../common.h"
#include "../util.h"
#include "../STRP/STRP.h"
#include "../ProtoMon/ProtoMon.h"

// Configuration flags

static LogLevel loglevel = INFO;
static const char *outputDir = "results";

static uint8_t self;
static unsigned int sleepDuration;

static pthread_t recvT;
static pthread_t sendT;

static void *sendMsg_func(void *args);
static void *recvMsg_func(void *args);

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

	// Create results dir
	char cmd[150];
	sprintf(cmd, "[ -d '%s' ] || mkdir -p '%s' && cp '../benchmark/send.csv' '%s/send.csv'", outputDir, outputDir, outputDir);
	if (system(cmd) != 0)
	{
		logMessage(ERROR, "%s - Error creating results dir\n", __func__);
		fflush(stdout);
		exit(EXIT_FAILURE);
	}

	fflush(stdout);

	ProtoMon_Config config;
	config.vizIntervalS = 60;
	config.loglevel = INFO;
	config.sendIntervalS = 20;
	config.self = self;
	config.monitoredLevels = PROTOMON_LEVEL_ALL;
	config.initialSendWaitS = 30;
	// ProtoMon_init(config);

	STRP_Config strp;
	strp.beaconIntervalS = 30;
	strp.loglevel = INFO;
	strp.nodeTimeoutS = 60;
	strp.recvTimeoutMs = 1000;
	strp.self = self;
	strp.senseDurationS = 15;
	strp.strategy = NEXT_LOWER;
	STRP_init(strp);

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
	logMessage(INFO, "Shutting down\n");
	fflush(stdout);
	return 0;
}

static void *recvMsg_func(void *args)
{
	unsigned int total[MAX_ACTIVE_NODES] = {0};
	Routing_Header *header = (Routing_Header *)args;
	char outputFile[100];
	sprintf(outputFile, "%s/recv.csv", outputDir);
	const char *cols = "Timestamp,Source,Address,SeqId,SentTimestamp,RecvTimestamp";

	FILE *file = fopen(outputFile, "w");
	if (file == NULL)
	{
		logMessage(ERROR, "Failed to open recv.csv\n");
		fflush(stdout);
		exit(EXIT_FAILURE);
	}
	fprintf(file, "%s\n", cols);
	fflush(file);

	while (1)
	{
		unsigned char buffer[240];
		int msgLen = Routing_timedRecvMsg(header, buffer, 1);
		if (msgLen > 0)
		{
			buffer[msgLen] = '\0'; // Null-terminate the string
			long long currentTimeMs = getEpochMs();

			char seqId[4];
			char timestamp[14];
			if (sscanf(buffer, "%[^_]_%s", &seqId, &timestamp) == 2)
			{
				fprintf(file, "%lld,%02d,%02d,%s,%s,%lld\n", currentTimeMs, self, header->src, seqId, timestamp, currentTimeMs);
				logMessage(INFO, "RX: %02d (%02d) src: %02d msg: %s total: %02d\n", header->prev, header->RSSI, header->src, seqId, ++total[header->src]);
				fflush(file);
				fflush(stdout);
			}
			else
			{
				logMessage(ERROR, "Malformed message received: %s\n", buffer);
			}
		}
		usleep((rand() % 200000) + 1000000); // Sleep 1-1.2s to prevent busy waiting
	}

	fclose(file);
	return NULL;
}

static void *sendMsg_func(void *args)
{
	Routing_Header *header = (Routing_Header *)args;
	unsigned int total, numLine;
	unsigned long prevSleep, waitIdle;
	char inputFile[100];
	sprintf(inputFile, "%s/send.csv", outputDir);
	// Read from config.txt
	FILE *file = fopen(inputFile, "r");
	if (file == NULL)
	{
		logMessage(ERROR, "Failed to open config.txt\n");
		fflush(stdout);
		exit(EXIT_FAILURE);
	}
	char line[256];
	while (fgets(line, sizeof(line), file) != NULL)
	{
		++numLine;
		if (numLine == 1)
		{
			continue; // Skip header line
		}

		if (loglevel == DEBUG)
		{
			logMessage(DEBUG, "Line: %s\n", line);
		}

		char sleep[10];
		char nodes[25];

		if (sscanf(line, "%[^,],%s", &nodes, &sleep) != 2)
		{
			logMessage(ERROR, "Line %d in %s: %s malformed\n", numLine, inputFile, line);
			continue;
		}

		unsigned long sendTime = atoll(sleep);
		if (loglevel == DEBUG)
		{
			logMessage(DEBUG, "Sleep: %s\n", sleep);
			logMessage(DEBUG, "Nodes: %s\n", nodes);
			fflush(stdout);
		}

		// List of nodes for which the config applies
		// * for all nodes, !xx|yy|zz to exclude node xx, yy & zz and xx|yy|zz for nodes node xx, yy & zz
		// Skip till nodes is * or does not contain self in !xx|yy|zz or xx|yy|zz
		if (nodes[0] == '*')
		{
			// Include all nodes, no filtering needed
			if (sendTime > waitIdle)
			{
				waitIdle = sendTime;
			}
		}
		else
		{
			int include = (nodes[0] != '!');
			char *token = strtok(include ? nodes : nodes + 1, "|");
			int match = 0;
			while (token != NULL)
			{
				if (atoi(token) == self)
				{
					match = 1;
					break;
				}
				token = strtok(NULL, "|");
			}
			if ((include && !match) || (!include && match))
			{
				if (sendTime > waitIdle)
				{
					waitIdle = sendTime;
				}
				continue; // Skip this line based on inclusion or exclusion logic
			}
		}

		long sleepMs = sendTime - prevSleep;
		prevSleep = sendTime;
		if (loglevel == DEBUG)
		{
			logMessage(INFO, "Sleep : %d ms\n", sleepMs);
			fflush(stdout);
		}
		usleep(sleepMs * 1000);

		char buffer[20];
		sprintf(buffer, "%03d_%lld", ++total, getEpochMs());

		uint8_t dest_addr = ADDR_SINK;

		if (Routing_sendMsg(dest_addr, buffer, strlen(buffer)))
		{
			logMessage(INFO, "TX: %02d msg: %s total: %02d\n", dest_addr, buffer, total);
			fflush(stdout);
		}
	}

	fclose(file);
	usleep(waitIdle * 1000);
	return NULL;
}
