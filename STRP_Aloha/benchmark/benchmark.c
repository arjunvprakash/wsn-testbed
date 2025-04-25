#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>
#include <signal.h>

#include "../common.h"
#include "../util.h"
#include "../STRP/STRP.h"
#include "../ProtoMon/ProtoMon.h"
#define HTTP_PORT 8000

// Configuration flags

static LogLevel loglevel = INFO;
static const char *outputDir = "benchmark";
static const char *outputFile = "recv.csv";
static const char *inputFile = "send.csv";
static const char *resultsDir = "results";

static uint8_t self;
static unsigned int runtTimeS = 120;
static unsigned int startTime;
static unsigned long long startTimeMs;

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

	fflush(stdout);

	ProtoMon_Config config;
	config.vizIntervalS = 60;
	config.loglevel = INFO;
	config.sendIntervalS = 30;
	config.self = self;
	config.monitoredLevels = PROTOMON_LEVEL_ALL;
	config.initialSendWaitS = 15;
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

	startTime = time(NULL);
	startTimeMs = getEpochMs();
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

static void installDependencies()
{

	if (loglevel >= DEBUG)
	{
		logMessage(DEBUG, "Installing dependencies...\n");
	}
	char *setenv_cmd = "pip install -r ../../benchmark/viz/requirements.txt > /dev/null &";
	if (loglevel == TRACE)
	{
		logMessage(TRACE, "Executing command : %s\n", setenv_cmd);
	}
	if (system(setenv_cmd) != 0)
	{
		logMessage(ERROR, "Error installing dependencies. Exiting...\n");
		fflush(stdout);
		exit(EXIT_FAILURE);
	}
	if (loglevel >= DEBUG)
	{
		logMessage(DEBUG, "Successfully installed dependencies...\n");
	}
}

// Check if a process is running on the specified TCP port and kill it
int killProcessOnPort(int port)
{
	char cmd[100];
	sprintf(cmd, "fuser -k %d/tcp > /dev/null 2>&1", port);
	// exit code check fails if no process was running
	int v = system(cmd);
	if (loglevel >= DEBUG)
	{
		logMessage(DEBUG, "Port %d freed\n", port);
	}
}

static void signalHandler(int signum)
{
	if (signum == SIGINT || signum == SIGTERM || signum == SIGABRT || signum == SIGSEGV || signum == SIGILL || signum == SIGFPE)
	{
		{
			killProcessOnPort(HTTP_PORT);
			if (loglevel >= DEBUG)
			{
				logMessage(DEBUG, "Stopped HTTP server on port %d\n", HTTP_PORT);
			}
		}
		exit(EXIT_SUCCESS);
	}
}

void createHttpServer(int port)
{
	char cmd[100];
	sprintf(cmd, "python3 -m http.server %d --bind 0.0.0.0 > httplogs.txt 2>&1 &", port);
	if (system(cmd) != 0)
	{
		logMessage(ERROR, "Error starting HTTP server\n");
		fflush(stdout);
		exit(EXIT_FAILURE);
	}
	if (loglevel >= DEBUG)
	{
		logMessage(DEBUG, "HTTP server started on port: %d\n", port);
		fflush(stdout);
	}
}

static void generateGraph()
{
	installDependencies();

	chdir(resultsDir);
	char cmd[100];
	sprintf(cmd, "python ../../benchmark/viz/script.py %lld", startTimeMs);
	if (system(cmd) != 0)
	{
		logMessage(ERROR, "Error generating graph\n");
		exit(EXIT_FAILURE);
	}
	else
	{
		logMessage(INFO, "Benchmark results: http://localhost:%d/benchmark\n", HTTP_PORT);
	}
	fflush(stdout);

	sprintf(cmd, "fuser %d/tcp > /dev/null 2>&1", HTTP_PORT);
	// int v = system(cmd);
	if (system(cmd) != 0)
	{
		killProcessOnPort(HTTP_PORT);
		createHttpServer(HTTP_PORT);

		// Register signal handler to stop the HTTP server on exit
		signal(SIGINT, signalHandler);
		signal(SIGTERM, signalHandler);
		signal(SIGILL, signalHandler);
		signal(SIGABRT, signalHandler);
		signal(SIGSEGV, signalHandler);
		signal(SIGFPE, signalHandler);
	}
}

static void *recvMsg_func(void *args)
{
	unsigned int total[MAX_ACTIVE_NODES] = {0};
	Routing_Header *header = (Routing_Header *)args;
	unsigned int count = 0;

	// Print current working directory
	char cmd[150];
	char cwd[1024];
	getcwd(cwd, sizeof(cwd));
	// With ProtoMon enabled cwd: Debug/results
	// With ProtoMon disabled cwd: Debug
	// if results not exist create it and chdir to Debug/results
	if (strstr(cwd, resultsDir) == NULL)
	{
		sprintf(cmd, "[ -d '%s' ] || mkdir -p '%s'", resultsDir, resultsDir);
		system(cmd);
		// Change to the results directory
		chdir(resultsDir);
	}

	// Create output dir
	sprintf(cmd, "[ -d '%s' ] || mkdir -p '%s' && cp '../../benchmark/viz/index.html' '%s/index.html'", outputDir, outputDir, outputDir);
	if (system(cmd) != 0)
	{
		logMessage(ERROR, "%s - Error creating results dir\n", __func__);
		fflush(stdout);
		exit(EXIT_FAILURE);
	}

	char filePath[100];
	sprintf(filePath, "%s/%s", outputDir, outputFile);
	const char *cols = "Timestamp,Source,Address,SeqId,SentTimestamp,RecvTimestamp";

	FILE *file = fopen(filePath, "w");
	if (file == NULL)
	{
		logMessage(ERROR, "Failed to open recv.csv\n");
		fflush(stdout);
		exit(EXIT_FAILURE);
	}
	fprintf(file, "%s\n", cols);
	fflush(file);

	while (time(NULL) - startTime < runtTimeS)
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
				fprintf(file, "%lld,%02d,%02d,%s,%s,%lld\n", count++ == 0 ? startTimeMs : currentTimeMs, self, header->src, seqId, timestamp, currentTimeMs);
				logMessage(INFO, "RX: %02d (%02d) src: %02d msg: %s total: %02d\n", header->prev, header->RSSI, header->src, seqId, ++total[header->src]);
				fflush(stdout);
			}
			else
			{
				logMessage(ERROR, "Malformed message received: %s\n", buffer);
			}
		}
		usleep((rand() % 200000) + 1000000); // Sleep 1-1.2s to prevent busy waiting
	}
	fflush(file);
	fclose(file);

	generateGraph();

	sleep(600);
	return NULL;
}

static void *sendMsg_func(void *args)
{
	Routing_Header *header = (Routing_Header *)args;
	unsigned int total, numLine;
	unsigned long prevSleep, waitIdle;
	char filePath[100];
	sprintf(filePath, "../benchmark/%s", inputFile);
	// Read from config.txt
	FILE *file = fopen(filePath, "r");
	if (file == NULL)
	{
		logMessage(ERROR, "Failed to open config.txt\n");
		fflush(stdout);
		exit(EXIT_FAILURE);
	}
	char line[256];
	while (time(NULL) - startTime < runtTimeS)
	{
		if (fgets(line, sizeof(line), file) == NULL)
		{
			break; // End of file reached
		}
		if (++numLine == 1)
		{
			continue; // Skip header line
		}

		if (loglevel == DEBUG)
		{
			logMessage(DEBUG, "Line: %s\n", line);
		}

		char sleep[10];
		char nodes[25];
		char dest[2];

		if (sscanf(line, "%[^,],%[^,],%s", &nodes, &sleep, &dest) != 3)
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

		uint8_t dest_addr = atoi(dest);

		if (Routing_sendMsg(dest_addr, buffer, strlen(buffer)))
		{
			logMessage(INFO, "TX: %02d msg: %s total: %02d\n", dest_addr, buffer, total);
			fflush(stdout);
		}
	}
	fclose(file);
	sleep(runtTimeS - (time(NULL) - startTime)); // Sleep for the remaining time
	return NULL;
}
