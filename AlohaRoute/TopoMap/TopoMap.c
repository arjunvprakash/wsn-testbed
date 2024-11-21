#include "TopoMap.h"

#include <stdio.h>     // printf
#include <stdlib.h>    // rand, malloc, free, exit

#include "../common.h"
#include "../util.h"

static const char *outputCSV = "/home/pi/sw_workspace/AlohaRoute/Debug/results/network.csv";
static LogLevel loglevel;

static void writeToCSVFile(NodeRoutingTable table);
static int killProcessOnPort(int port);
static void installDependencies();
static void createHttpServer();
static void createCSVFile();


static void writeToCSVFile(NodeRoutingTable table)
{
    FILE *file = fopen(outputCSV, "a");
    if (file == NULL)
    {
        printf("## - Error opening csv file!\n");
        exit(EXIT_FAILURE);
    }
    if (loglevel >= DEBUG)
    {
        printf("### Writing to CSV\n");
        printf("Timestamp, Source, Address, Active, Role, RSSI, Parent, ParentRSSI\n");
    }
    for (int i = 0; i < table.numActive; i++)
    {
        NodeInfo node = table.nodes[i];
        fprintf(file, "%s,%02d,%02d,%d,%s,%d,%02d,%d\n", table.timestamp, table.src, node.addr, node.isActive, getRoleStr(node.role), node.RSSI, node.parent, node.parentRSSI);
        if (loglevel >= DEBUG)
        {
            printf("%s, %02d, %02d, %d, %s, %d, %02d, %d\n", table.timestamp, table.src, node.addr, node.isActive, getRoleStr(node.role), node.RSSI, node.parent, node.parentRSSI);
        }
    }
    fclose(file);
    fflush(stdout);
}


static void createCSVFile()
{
    const char *cmd = "[ -d '/home/pi/sw_workspace/AlohaRoute/Debug/results' ] || mkdir -p '/home/pi/sw_workspace/AlohaRoute/Debug/results' && cp '/home/pi/sw_workspace/AlohaRoute/logs/index.html' '/home/pi/sw_workspace/AlohaRoute/Debug/results/index.html'";
    if (system(cmd) != 0)
    {
        printf("## - Error creating results dir!\n");
        exit(EXIT_FAILURE);
    }
    FILE *file = fopen(outputCSV, "w");
    if (file == NULL)
    {
        printf("## - Error creating csv file!\n");
        exit(EXIT_FAILURE);
    }
    const char *header = "Timestamp,Source,Address,Active,Role,RSSI,Parent,ParentRSSI\n";
    fprintf(file, "%s", header);
    fclose(file);
    if (loglevel >= DEBUG)
    {
        printf("### CSV file: %s created\n", outputCSV);
    }
}