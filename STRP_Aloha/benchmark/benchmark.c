#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>

#include "../common.h"
#include "../util.h"
#include "../STRP/STRP.h"
#include "../ProtoMon/ProtoMon.h"

#define HTTP_PORT 8000
#define CSV_COLS 4

// Configuration flags

static LogLevel loglevel = INFO;
static const char *outputDir = "benchmark";
static const char *outputFile = "recv.csv";
static const char *inputFile = "send.csv";
static const char *resultsDir = "results";

static uint8_t self;
static unsigned int startTime;
static unsigned long long startTimeMs;
static char configStr[300];
static const unsigned short minPayloadSize = 20; // self (2) + '_'(1) + seqId (3) + '_' (1) + timestamp_ms (13)

static pthread_t recvT;
static pthread_t sendT;

ProtoMon_Config config;
STRP_Config strp;
static uint8_t parentTable[MAX_ACTIVE_NODES], hopCountTable[MAX_ACTIVE_NODES], pktCountTable[MAX_ACTIVE_NODES];

static bool monitoringEnabled = false;
static unsigned int runtTimeS = 120;
static unsigned short maxSyncSleepS = 10;
static unsigned short senseDurationS = 5;

static void initParentTable()
{
	// parentTable[1] = 5;
	// parentTable[4] = 5;
	// parentTable[5] = 7;
	// parentTable[6] = 7;
	parentTable[7] = 13;
	parentTable[8] = 13;
	// parentTable[9] = ADDR_SINK;
	// parentTable[12] = 15;
	parentTable[13] = ADDR_SINK;
	// parentTable[14] = 0; // Sink
	// parentTable[15] = 16;
	// parentTable[16] = 19;
	parentTable[18] = ADDR_SINK;
	// parentTable[19] = ADDR_SINK;
	parentTable[20] = 18;
	// parentTable[21] = 23;
	parentTable[22] = 18;
	// // parentTable[23] = 25;
	parentTable[24] = 22;
	// parentTable[25] = 28;
	parentTable[27] = 22;
	// parentTable[28] = ADDR_SINK;
}

static unsigned short getHopCount(uint8_t node)
{
	uint8_t parent = parentTable[node];
	if (node == ADDR_SINK || parent == 0)
	{
		return 0;
	}
	if (parent == ADDR_SINK)
	{
		return 1;
	}
	else
	{
		if (hopCountTable[parent] == 0)
		{
			hopCountTable[parent] = getHopCount(parent);
		}

		return 1 + hopCountTable[parent];
	}
}

static void initHopCountTable()
{
	if (loglevel >= DEBUG)
	{
		logMessage(DEBUG, "------\n");
		logMessage(DEBUG, "HopCount:\n");
	}
	for (uint8_t i = 1; i < MAX_ACTIVE_NODES; i++)
	{
		if (parentTable[i] > 0)
		{
			hopCountTable[i] = getHopCount(i);
			if (loglevel >= DEBUG)
			{
				logMessage(DEBUG, "%02d: %d\n", i, hopCountTable[i]);
			}
		}
	}
	if (loglevel >= DEBUG)
	{
		logMessage(DEBUG, "------");
	}
}

static void initPktCountTable()
{
	int numLine;
	char filePath[100];
	sprintf(filePath, "../../benchmark/%s", inputFile);
	// Read from config.txt
	FILE *file = fopen(filePath, "r");
	if (file == NULL)
	{
		logMessage(ERROR, "Failed to open %s\n", filePath);
		fflush(stdout);
		exit(EXIT_FAILURE);
	}
	char line[256];
	while (fgets(line, sizeof(line), file) != NULL)
	{
		if (numLine++ == 0)
		{
			continue; // Skip header line
		}

		char sleep[10];
		char nodes[100];
		char dest[2];
		char size[3];

		if (sscanf(line, "%[^,],%[^,],%[^,],%s", &nodes, &sleep, &dest, &size) != CSV_COLS)
		{
			logMessage(ERROR, "Line %d in %s: %s malformed\n", numLine, inputFile, line);
			fflush(stdout);
			// exit(EXIT_FAILURE);
			continue;
		}

		if (loglevel >= TRACE)
		{
			logMessage(TRACE, "Nodes Expr: %s\n", nodes);
		}

		if (nodes[0] == '*')
		{
			// Include all nodes, no filtering needed
			pktCountTable[0]++;
		}
		else
		{
			int include = (nodes[0] != '!');
			if (!include && nodes[1] == '*')
			{
				continue;
			}

			char *token = strtok(include ? nodes : nodes + 1, "|");
			int match = 0;
			while (token != NULL)
			{
				uint8_t addr = atoi(token);
				if (include)
				{
					pktCountTable[addr]++;
				}
				else
				{
					pktCountTable[addr]--;
				}
				if (loglevel >= TRACE)
				{
					logMessage(TRACE, "%02d: %d\n", addr, pktCountTable[addr]);
				}
				token = strtok(NULL, "|");
			}
		}
	}
	fclose(file);
	if (loglevel >= DEBUG)
	{
		logMessage(DEBUG, "------\n");
		logMessage(DEBUG, "Packet Count:\n");
	}
	for (int i = 1; i < MAX_ACTIVE_NODES; i++)
	{

		if (parentTable[i] > 0)
		{
			pktCountTable[i] += pktCountTable[0];
			if (loglevel >= DEBUG)
			{
				logMessage(DEBUG, "%02d: %d\n", i, pktCountTable[i]);
			}
		}
	}
	if (loglevel >= DEBUG)
	{
		logMessage(DEBUG, "------\n");
	}
}

// List of nodes for which the config applies
// * to include all nodes
//  !xx|yy|zz to exclude node xx, yy & zz and
//  xx|yy|zz to include nodes xx, yy & zz
static int searchNodesExpr(char *nodes, uint8_t addr)
{
	if (nodes[0] == '*')
	{
		// Include all nodes, no filtering needed
		return 1;
	}
	else
	{
		int include = (nodes[0] != '!');
		if (!include && nodes[1] == '*')
		{
			return 0;
		}

		char *token = strtok(include ? nodes : nodes + 1, "|");
		int match = 0;
		while (token != NULL)
		{
			if (atoi(token) == addr)
			{
				match = 1;
				break;
			}
			token = strtok(NULL, "|");
		}
		if ((include && !match) || (!include && match))
		{
			return 0; // addr is not included
		}
		return 1;
	}
}

static void *sendMsg_func(void *args);
static void *recvMsg_func(void *args);
static int stopProcessOnPort(int port);
static void exitHandler(int signum);
static void generateGraph();
static void getConfigStr(char *configStr, ProtoMon_Config protomon, STRP_Config strp);
static void syncTime(unsigned int n);
static void installDependencies();
static void startHttpServer(int port);
static void initOutputDir();

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

	initParentTable();
	if (self == ADDR_SINK)
	{
		initHopCountTable();
	}

	strp.beaconIntervalS = 32;
	strp.loglevel = INFO;
	strp.nodeTimeoutS = 63;
	strp.recvTimeoutMs = 1000;
	strp.self = self;
	strp.senseDurationS = senseDurationS;
	strp.strategy = FIXED;
	strp.parentTable = parentTable;
	STRP_init(strp);

	config.vizIntervalS = 60;
	config.loglevel = INFO;
	config.sendIntervalS = 30;
	config.self = self;
	config.monitoredLevels = PROTOMON_LEVEL_ALL;
	config.initialSendWaitS = maxSyncSleepS;
	if (monitoringEnabled)
	{
		ProtoMon_init(config);
	}

	if (self == ADDR_SINK)
	{
		initOutputDir();
		initPktCountTable();
		installDependencies();
		if (!monitoringEnabled)
		{
			stopProcessOnPort(HTTP_PORT);
			startHttpServer(HTTP_PORT);
		}
	}
	syncTime(maxSyncSleepS);

	startTime = time(NULL);
	startTimeMs = getEpochMs();

	getConfigStr(configStr, config, strp);
	logMessage(INFO, "Config:\n%s\n", configStr);
	fflush(stdout);

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

		generateGraph();

		sleep(1800);

		stopProcessOnPort(HTTP_PORT);
	}
	logMessage(INFO, "Shutting down\n");
	fflush(stdout);
	return 0;
}

static void syncTime(unsigned int n)
{
	// Sleep until the next multiple of n seconds
	time_t now = time(NULL);
	struct tm *wakeUpTime = localtime(&now);
	unsigned short seconds = n - (wakeUpTime->tm_sec % n);
	if (seconds < n)
	{
		logMessage(INFO, "Sleeping for %d seconds\n", seconds);
		fflush(stdout);
		sleep(seconds);
	}
}

// Generate a string with the experiment configuration
static void getConfigStr(char *configStr, ProtoMon_Config protomon, STRP_Config strp)
{
	sprintf(configStr, "Application: self=%d,runtTimeS=%d\n", self, runtTimeS);
	if (monitoringEnabled)
	{
		sprintf(configStr + strlen(configStr), "ProtoMon: vizIntervalS=%d,sendIntervalS=%d,monitoredLevels=%d,initialSendWaitS=%d\n",
				protomon.vizIntervalS, protomon.sendIntervalS, protomon.monitoredLevels, protomon.initialSendWaitS);
	}
	else
	{
		sprintf(configStr + strlen(configStr), "ProtoMon: disabled\n");
	}
	sprintf(configStr + strlen(configStr), "STRP: beaconIntervalS=%d,nodeTimeoutS=%d,recvTimeoutMs=%d,senseDurationS=%d,strategy=%d\n",
			strp.beaconIntervalS, strp.nodeTimeoutS, strp.recvTimeoutMs, strp.senseDurationS, strp.strategy);
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
int stopProcessOnPort(int port)
{
	char cmd[100];
	sprintf(cmd, "fuser -k %d/tcp > /dev/null 2>&1", port);
	// exit code check fails if no process was running
	int v = system(cmd);
	if (loglevel >= DEBUG)
	{
		logMessage(DEBUG, "Port %d freed\n", port);
	}
	return v;
}

static void exitHandler(int signum)
{
	if (signum == SIGINT || signum == SIGTERM || signum == SIGABRT || signum == SIGSEGV || signum == SIGILL || signum == SIGFPE)
	{
		{
			stopProcessOnPort(HTTP_PORT);
			// if (loglevel >= DEBUG)
			{
				logMessage(DEBUG, "Stopped HTTP server on port %d\n", HTTP_PORT);
			}
		}
		exit(EXIT_SUCCESS);
	}
}

void startHttpServer(int port)
{
	char cmd[100];
	sprintf(cmd, "python3 -m http.server %d --bind 0.0.0.0 > httplogs.txt 2>&1&", port);
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

	// Register signal handler to stop the HTTP server on exit
	signal(SIGINT, exitHandler);
	signal(SIGTERM, exitHandler);
	signal(SIGILL, exitHandler);
	signal(SIGABRT, exitHandler);
	signal(SIGSEGV, exitHandler);
	signal(SIGFPE, exitHandler);
}

static void generateGraph()
{
	// chdir(resultsDir);

	char cmd[500];
	sprintf(cmd, "python ../../benchmark/viz/script.py --config='%s'", configStr);
	if (system(cmd) != 0)
	{
		logMessage(ERROR, "Error generating graph\n");
		fflush(stdout);
		exit(EXIT_FAILURE);
	}
	else
	{
		logMessage(INFO, "Benchmark results: http://localhost:%d/benchmark\n", HTTP_PORT);
	}
	fflush(stdout);
}

static void *recvMsg_func(void *args)
{
	unsigned int total[MAX_ACTIVE_NODES] = {0};
	Routing_Header *header = (Routing_Header *)args;
	unsigned int count = 0;

	char filePath[100];
	sprintf(filePath, "%s/%s", outputDir, outputFile);
	FILE *file = fopen(filePath, "a");

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
			char filler[25];
			char addr[3];
			// if (loglevel >= DEBUG)
			{
				logMessage(DEBUG, "Buffer: %s\n", buffer);
				fflush(stdout);
			}

			if (sscanf(buffer, "%[^_]_%[^_]_%[^_]_%s", addr, seqId, timestamp, filler) >= 3)
			{
				uint8_t src = (uint8_t)atoi(addr);
				fprintf(file, "%lld,%02d,%02d,%s,%s,%lld,%d\n", count++ == 0 ? startTimeMs : currentTimeMs, self, src, seqId, timestamp, currentTimeMs, hopCountTable[src]);
				logMessage(INFO, "RX: %02d (%02d) src:%02d (%02d) hops:%d seqId:%s total:%02d\n", header->prev, header->RSSI, header->src, src, hopCountTable[src], seqId, ++total[src]);
				fflush(stdout);
			}
			else
			{
				logMessage(ERROR, "Malformed message received from src: %02d, buffer: %s\n", header->src, buffer);
			}
		}
		usleep((rand() % 200000) + 1000000); // Sleep 1-1.2s to prevent busy waiting
	}
	fflush(file);
	fclose(file);

	return NULL;
}

static void initOutputDir()
{
	char cmd[150];

	// With ProtoMon enabled cwd: Debug/results
	// With ProtoMon disabled cwd: Debug
	// if results not exist create it and chdir to Debug/results

	// char cwd[1024];
	// getcwd(cwd, sizeof(cwd));
	// if (strstr(cwd, resultsDir) == NULL)
	if (!monitoringEnabled)
	{
		// Create results dir
		sprintf(cmd, "[ -d '%s' ] || mkdir -p '%s'", resultsDir, resultsDir);
		if (system(cmd) != 0)
		{
			logMessage(ERROR, "Failed to create results directory: %s\n", resultsDir);
			fflush(stdout);
			exit(EXIT_FAILURE);
		}

		// Change to the results dir
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
	const char *cols = "Timestamp,Source,Address,SeqId,SentTimestamp,RecvTimestamp,HopCount";

	FILE *file = fopen(filePath, "w");
	if (file == NULL)
	{
		logMessage(ERROR, "Failed to open %s\n", filePath);
		fflush(stdout);
		exit(EXIT_FAILURE);
	}
	fprintf(file, "%s\n", cols);
	fflush(file);
	fclose(file);
}

static void *sendMsg_func(void *args)
{
	Routing_Header *header = (Routing_Header *)args;
	int total, numLine;
	unsigned long prevSleep, waitIdle;
	char filePath[100];
	sprintf(filePath, "../benchmark/%s", inputFile);
	// Read from config.txt
	FILE *file = fopen(filePath, "r");
	if (file == NULL)
	{
		logMessage(ERROR, "Failed to open %s\n", filePath);
		fflush(stdout);
		exit(EXIT_FAILURE);
	}
	char line[256];
	while ((time(NULL) - startTime) < runtTimeS)
	{
		if (fgets(line, sizeof(line), file) == NULL)
		{
			break; // End of file reached
		}
		if (numLine++ == 0)
		{
			continue; // Skip header line
		}

		// if (loglevel == DEBUG)
		{
			logMessage(DEBUG, "Line: %s\n", line);
		}

		char sleep[10];
		char nodes[100];
		char dest[2];
		char size[3];

		if (sscanf(line, "%[^,],%[^,],%[^,],%s", &nodes, &sleep, &dest, &size) != CSV_COLS)
		{
			logMessage(ERROR, "Line %d in %s: %s malformed\n", numLine, inputFile, line);
			fflush(stdout);
			exit(EXIT_FAILURE);
			// continue;
		}

		unsigned long sendTime = atoll(sleep);
		unsigned short payloadSize = atoi(size);
		uint8_t dest_addr = atoi(dest);

		if (searchNodesExpr(nodes, self))
		{
			if (sendTime > waitIdle)
			{
				waitIdle = sendTime;
			}
			else
			{
				logMessage(ERROR, "Line %d : Delta to SendTime negative on Node %02d.\n", numLine, self);
				fflush(stdout);
				exit(EXIT_FAILURE);
			}
		}
		else
		{
			if (loglevel >= DEBUG)
			{
				logMessage(DEBUG, "Line %d: node %02d excluded.\n", numLine, self);
				fflush(stdout);
			}

			// Skip the line, continue
			continue;
		}

		if (loglevel == DEBUG)
		{
			logMessage(DEBUG, "SendTime: %ld, Destination: %02d\n", sendTime, dest_addr);
			fflush(stdout);
		}

		long sleepMs = sendTime - prevSleep;
		prevSleep = sendTime;
		if (loglevel == DEBUG)
		{
			logMessage(INFO, "Sleep : %d ms\n", sleepMs);
			fflush(stdout);
		}

		if ((time(NULL) - startTime) >= runtTimeS)
		{
			fclose(file);
			return NULL;
		}

		usleep(sleepMs * 1000);

		char buffer[240];
		if (payloadSize > minPayloadSize)
		{
			// Generate a random string of size payloadSize - 1 (excluding _ separator)
			char additionalBytes[payloadSize - minPayloadSize];
			additionalBytes[0] = '_';
			if (payloadSize - minPayloadSize > 1)
			{
				memset(additionalBytes + 1, 'a' + ((payloadSize - minPayloadSize) % 26), payloadSize - minPayloadSize - 1);
			}

			sprintf(buffer, "%02d_%03d_%lld%s", self, ++total, getEpochMs(), additionalBytes);
		}
		else
		{
			if (payloadSize < minPayloadSize)
			{
				logMessage(DEBUG, "Payload size %d < minimum %d.Sending default.\n", payloadSize, minPayloadSize);
				fflush(stdout);
			}
			sprintf(buffer, "%02d_%03d_%lld", self, ++total, getEpochMs());
		}

		if (Routing_sendMsg(dest_addr, buffer, strlen(buffer)))
		{
			logMessage(INFO, "TX: %02d msg: %s total: %02d\n", dest_addr, buffer, total);
		}
		else
		{
			logMessage(ERROR, "Failed sending packet to Pi %02d\n", dest_addr);
		}
		fflush(stdout);
	}
	fclose(file);
	unsigned long idleTime = runtTimeS - (time(NULL) - startTime);
	if (idleTime > 0)
	{
		logMessage(INFO, "Waiting %d seconds for forwarding\n", idleTime);
		fflush(stdout);
		sleep(idleTime); // Sleep for the remaining time
	}
	return NULL;
}
