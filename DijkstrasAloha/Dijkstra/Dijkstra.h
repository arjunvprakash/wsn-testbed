#ifndef DIJKSTRA_H
#define DIJKSTRA_H

#include "../ALOHA/ALOHA.h"

// maximale Nachrichtenlänge
#define max_msg_len (240 - Routing_Header_len - MAC_Header_len)

// Anzahl Netzwerkteilnehmer
#define anz_knoten 9
#define min_addr 7

// Struktur für den Nachrichtenheader
typedef struct Routing_Header {
	uint8_t ctrl;			// Kontrollflag
	uint8_t src;		// Absenderadresse
	uint8_t dst_addr;		// Zieladresse
	int RSSI;
	uint8_t prev;
} Routing_Header;
#define Routing_Header_len 3

typedef struct Routing {
	/* konfigurierbare Parameter */
	MAC mac;				// Struktur der MAC-Schicht
	
	/* Daten zur letzten empfangenen Nachricht */
    Routing_Header recvH;	// Nachrichteheader
	int RSSI;				// RSSI-Wert

	/* Sonstige */
    int debug;				// Gibt an, ob Debug-Ausgaben erstellt werden sollen
} Routing;

void Dijkstras_init(Routing*, unsigned char);

int Dijkstras_send(uint8_t dest, uint8_t *data, unsigned int len);
int Dijkstras_recv(Routing_Header *h, uint8_t *data);
int Dijkstras_timedrecv(Routing_Header *h, uint8_t *data, unsigned int timeout);

// Access points for ProtoMon
extern int (*Routing_sendMsg)(uint8_t dest, uint8_t *data, unsigned int len);
extern int (*Routing_recvMsg)(Routing_Header *h, uint8_t *data);
extern int (*Routing_timedRecvMsg)(Routing_Header *h, uint8_t *data, unsigned int timeout);
uint8_t Routing_getHeaderSize();
uint8_t Routing_isDataPkt(uint8_t ctrl);
uint8_t *Routing_getMetricsHeader();
uint8_t *Routing_getNeighbourHeader();
int Routing_getMetricsData(uint8_t *buffer, uint8_t addr);
int Routing_getNeighbourData(char *buffer, uint16_t size);

#endif //DIJKSTRA_H
