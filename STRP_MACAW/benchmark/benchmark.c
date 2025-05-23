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

typedef struct Benchmark_Config
{
	uint8_t self;
	bool monitoringEnabled;
	unsigned int runtTimeS;
	unsigned short maxSyncSleepS;
	unsigned short senseDurationS;
	uint8_t parentTable[MAX_ACTIVE_NODES], nodeCount;
	uint8_t hopCountTable[MAX_ACTIVE_NODES], minHopCount, maxHopCount;
	uint8_t pktCountTable[MAX_ACTIVE_NODES], minPktCount, maxPktCount;
	char *name;
} Benchmark_Config;

static unsigned int startTime;
static unsigned long long startTimeMs;
static char configStr[1024];
static const unsigned short minPayloadSize = 20; // self (2) + '_'(1) + seqId (3) + '_' (1) + timestamp_ms (13)
static const short minSendIntervalS = 60;

static pthread_t recvT;
static pthread_t sendT;

Benchmark_Config config;
ProtoMon_Config protomon;
STRP_Config strp;

static void initParentTable()
{
	config.parentTable[1] = 16;
	config.parentTable[4] = 9;
	config.parentTable[5] = 16;
	config.parentTable[6] = 16;
	config.parentTable[7] = 13;
	config.parentTable[8] = 13;
	config.parentTable[9] = 24;
	config.parentTable[12] = 21;
	config.parentTable[13] = ADDR_SINK;
	// // config.parentTable[14] = ADDR_SINK; // Sink
	// config.parentTable[15] = ADDR_SINK;
	config.parentTable[16] = 9;
	config.parentTable[18] = 22;
	// config.parentTable[19] = ADDR_SINK;
	config.parentTable[20] = 22;
	config.parentTable[21] = 9;
	config.parentTable[22] = ADDR_SINK;
	// config.parentTable[23] = ADDR_SINK;
	config.parentTable[24] = ADDR_SINK;
	config.parentTable[25] = 9;
	config.parentTable[27] = 22;
	// config.parentTable[28] = ADDR_SINK;
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
static unsigned short getHopCount(uint8_t node);
static int searchNodesExpr(char *nodes, uint8_t addr);
static void initPktCountTable();
static void initHopCountTable();

int main(int argc, char *argv[])
{
	// config.name = "Baseline: data@60-70"; //, routing metric@70-110";
	// config.name = "Equivalent: data@60-70, 40B@70-80";
	config.name = "Baseline: data@60-70, 40B metric@71-110 (based on hopcount)";

	config.self = (uint8_t)atoi(argv[1]);
	config.monitoringEnabled = false;
	config.runtTimeS = 350;
	config.maxSyncSleepS = 30;
	config.senseDurationS = 10;

	logMessage(INFO, "Node: %02d\n", config.self);
	logMessage(INFO, "Role : %s\n", config.self == ADDR_SINK ? "SINK" : "NODE");
	if (config.self != ADDR_SINK)
	{
		logMessage(INFO, "ADDR_SINK : %02d\n", ADDR_SINK);
	}
	srand(config.self);

	fflush(stdout);

	initParentTable();
	initHopCountTable();
	initPktCountTable();

	strp.beaconIntervalS = 65;
	strp.loglevel = INFO;
	strp.nodeTimeoutS = 200;
	strp.recvTimeoutMs = 1000;
	strp.self = config.self;
	strp.senseDurationS = config.senseDurationS;
	strp.strategy = FIXED;
	strp.parentAddr = config.parentTable[config.self];
	STRP_init(strp);

	syncTime(config.maxSyncSleepS);

	protomon.vizIntervalS = 60;
	protomon.loglevel = INFO;
	protomon.sendIntervalS = minSendIntervalS + 10 + (config.hopCountTable[config.self] * 5) + (config.self % 5);
	// protomon.sendIntervalS = 60;
	protomon.self = config.self;
	protomon.monitoredLevels = PROTOMON_LEVEL_ROUTING;
	protomon.initialSendWaitS = 15;
	if (config.monitoringEnabled)
	{
		ProtoMon_init(protomon);
	}

	if (config.self == ADDR_SINK)
	{
		initOutputDir();
		installDependencies();
		if (!config.monitoringEnabled)
		{
			stopProcessOnPort(HTTP_PORT);
			startHttpServer(HTTP_PORT);
		}
	}

	startTime = time(NULL);
	startTimeMs = getEpochMs();

	getConfigStr(configStr, protomon, strp);
	logMessage(INFO, "Config:\n%s\n", configStr);
	fflush(stdout);

	if (config.self != ADDR_SINK)
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
	sprintf(configStr, "%s\nApplication: self=%d,runtTimeS=%d,nodes=%d,hops=[%d to %d],pkt=[%d to %d]\n", config.name, config.self, config.runtTimeS, config.nodeCount, config.minHopCount, config.maxHopCount, config.minPktCount, config.maxPktCount);
	if (config.monitoringEnabled)
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
			if (loglevel >= DEBUG)
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
	unsigned int count = 0;

	char filePath[100];
	sprintf(filePath, "%s/%s", outputDir, outputFile);
	FILE *file = fopen(filePath, "a");

	while (time(NULL) - startTime < config.runtTimeS)
	{
		Routing_Header h;
		Routing_Header *header = &h;
		unsigned char buffer[240];
		int msgLen = Routing_timedRecvMsg(header, buffer, 1);
		if (msgLen > 0)
		{
			buffer[msgLen] = '\0'; // Null-terminate the string
			long long currentTimeMs = getEpochMs();

			char seqId[4];
			char timestamp[14];
			char filler[MAX_PAYLOAD_SIZE];
			char addr[3];
			if (loglevel >= DEBUG)
			{
				logMessage(DEBUG, "RX buffer: %s\n", buffer);
			}

			if (sscanf(buffer, "%[^_]_%[^_]_%[^_]_%s", addr, seqId, timestamp, filler) >= 3)
			{
				uint8_t src = (uint8_t)atoi(addr);
				++total[src];
				fprintf(file, "%lld,%02d,%02d,%s,%s,%lld,%d,%d,%d\n", count++ == 0 ? startTimeMs : currentTimeMs, config.self, src, seqId, timestamp, currentTimeMs, config.hopCountTable[src], config.pktCountTable[src], total[src]);
				logMessage(INFO, "RX: %02d (%02d) src:%02d hops:%d seqId:%s total:%d/%d\n", header->prev, header->RSSI, src, config.hopCountTable[src], seqId, total[src], config.pktCountTable[src]);
				fflush(stdout);
			}
			else
			{
				logMessage(ERROR, "Malformed message received from src: %02d, buffer: %s\n", header->src, buffer);
			}
		}
		usleep((rand() % 200000) + 500000); // Sleep 1-1.2s to prevent busy waiting
	}
	for (int i = 1; i < MAX_ACTIVE_NODES; i++)
	{
		if (config.parentTable[i] > 0)
		{
			fprintf(file, "%lld,%02d,%02d,%s,%s,%lld,%d,%d,%d\n", getEpochMs(), config.self, i, "", "", getEpochMs(), config.hopCountTable[i], config.pktCountTable[i], total[i]);
		}
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
	if (!config.monitoringEnabled)
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
	const char *cols = "Timestamp,Source,Address,SeqId,SentTimestamp,RecvTimestamp,HopCount,TotalCount,RecvCount";

	FILE *file = fopen(filePath, "w");
	if (file == NULL)
	{
		logMessage(ERROR, "%s: Failed to open %s\n", __func__, filePath);
		fflush(stdout);
		exit(EXIT_FAILURE);
	}
	fprintf(file, "%s\n", cols);
	fflush(file);
	fclose(file);
}

static void *sendMsg_func(void *args)
{
	int total;
	unsigned long prevSleep, waitIdle;
	char filePath[100];
	sprintf(filePath, "../benchmark/%s", inputFile);
	// Read from config.txt
	FILE *file = fopen(filePath, "r");
	if (file == NULL)
	{
		logMessage(ERROR, "%s: Failed to open %s\n", __func__, filePath);
		fflush(stdout);
		exit(EXIT_FAILURE);
	}

	int numLine = 0;
	while ((time(NULL) - startTime) < config.runtTimeS)
	{
		char line[256];
		if (fgets(line, sizeof(line), file) == NULL)
		{
			break; // End of file reached
		}

		++numLine;
		if (numLine == 1)
		{
			continue; // Skip header line
		}

		if (loglevel == DEBUG)
		{
			logMessage(DEBUG, "Line #%d: %s\n", numLine, line);
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

		if (searchNodesExpr(nodes, config.self))
		{
			if (sendTime > waitIdle)
			{
				waitIdle = sendTime;
			}
			else
			{
				logMessage(ERROR, "Line %d : Delta to SendTime negative on Node %02d.\n", numLine, config.self);
				fflush(stdout);
				exit(EXIT_FAILURE);
			}
		}
		else
		{
			if (loglevel >= DEBUG)
			{
				logMessage(DEBUG, "Line %d: node %02d excluded.\n", numLine, config.self);
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

		if ((time(NULL) - startTime) >= config.runtTimeS)
		{
			fclose(file);
			return NULL;
		}

		unsigned int sleepOffsetMs = (config.self % 10) * 1000;
		usleep((sleepMs + sleepOffsetMs) * 1000);

		char buffer[MAX_PAYLOAD_SIZE];
		if (payloadSize > minPayloadSize)
		{
			// Generate a random string of size payloadSize - 1 (excluding _ separator)
			char additionalBytes[payloadSize - minPayloadSize];
			additionalBytes[0] = '_';
			if (payloadSize - minPayloadSize > 1)
			{
				memset(additionalBytes + 1, 'a' + (rand() % 26), payloadSize - minPayloadSize - 1);
			}

			sprintf(buffer, "%02d_%03d_%lld%s", config.self, ++total, getEpochMs(), additionalBytes);
		}
		else
		{
			if (payloadSize < minPayloadSize)
			{
				logMessage(DEBUG, "Payload size %d < minimum %d.Sending default.\n", payloadSize, minPayloadSize);
				fflush(stdout);
			}
			sprintf(buffer, "%02d_%03d_%lld", config.self, ++total, getEpochMs());
		}

		if (Routing_sendMsg(dest_addr, buffer, strlen(buffer)))
		{
			logMessage(INFO, "TX: %02d msg: %s total: %d/%d\n", dest_addr, buffer, total, config.pktCountTable[config.self]);
		}
		else
		{
			logMessage(ERROR, "Failed sending packet to Pi %02d\n", dest_addr);
		}
		fflush(stdout);
	}
	fclose(file);
	unsigned long idleTime = config.runtTimeS - (time(NULL) - startTime);
	if (idleTime > 0)
	{
		logMessage(INFO, "Waiting %d seconds for forwarding\n", idleTime);
		fflush(stdout);
		sleep(idleTime); // Sleep for the remaining time
	}
	return NULL;
}

static unsigned short getHopCount(uint8_t node)
{
	uint8_t parent = config.parentTable[node];
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
		if (config.hopCountTable[parent] == 0)
		{
			config.hopCountTable[parent] = getHopCount(parent);
		}

		return 1 + config.hopCountTable[parent];
	}
}

static void initHopCountTable()
{
	config.minHopCount = 99;
	if (loglevel >= DEBUG)
	{
		logMessage(DEBUG, "------\n");
		logMessage(DEBUG, "HopCount:\n");
	}
	for (uint8_t i = 1; i < MAX_ACTIVE_NODES; i++)
	{
		if (config.parentTable[i] > 0)
		{
			config.hopCountTable[i] = getHopCount(i);
			config.nodeCount++;

			if (config.hopCountTable[i] > config.maxHopCount)
			{
				config.maxHopCount = config.hopCountTable[i];
			}
			else if (config.hopCountTable[i] < config.minHopCount)
			{
				config.minHopCount = config.hopCountTable[i];
			}

			if (loglevel >= DEBUG)
			{
				logMessage(DEBUG, "%02d: %d\n", i, config.hopCountTable[i]);
			}
		}
	}
	if (loglevel >= DEBUG)
	{
		logMessage(DEBUG, "------\n");
	}
}

static void initPktCountTable()
{
	config.minPktCount = 99;
	char filePath[100];
	sprintf(filePath, "../benchmark/%s", inputFile);
	// Read from config.txt
	FILE *file = fopen(filePath, "r");
	if (file == NULL)
	{
		logMessage(ERROR, "%s: Failed to open %s\n", __func__, filePath);
		fflush(stdout);
		exit(EXIT_FAILURE);
	}

	int numLine = 0;
	while (true)
	{
		char line[256];
		if (fgets(line, sizeof(line), file) == NULL)
		{
			break;
		}
		numLine++;

		if (numLine == 1)
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

		unsigned long sendTime = atoll(sleep);
		if (sendTime >= (config.runtTimeS * 1000))
		{
			// Beyond the runtimeS
			continue;
		}

		if (nodes[0] == '*')
		{
			// Include all nodes, no filtering needed
			config.pktCountTable[0]++;
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
					config.pktCountTable[addr]++;
				}
				else
				{
					config.pktCountTable[addr]--;
				}
				if (loglevel >= TRACE)
				{
					logMessage(TRACE, "%02d: %d\n", addr, config.pktCountTable[addr]);
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

		if (config.parentTable[i] > 0)
		{
			config.pktCountTable[i] += config.pktCountTable[0];
			if (config.pktCountTable[i] > config.maxPktCount)
			{
				config.maxPktCount = config.pktCountTable[i];
			}
			else if (config.pktCountTable[i] < config.minPktCount)
			{
				config.minPktCount = config.pktCountTable[i];
			}

			if (loglevel >= DEBUG)
			{
				logMessage(DEBUG, "%02d: %d\n", i, config.pktCountTable[i]);
			}
		}
	}
	if (loglevel >= DEBUG)
	{
		logMessage(DEBUG, "%02d: %d\n", 0, config.pktCountTable[0]);
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