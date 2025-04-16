#ifndef DIJKSTRA_H
#define DIJKSTRA_H

#include "../MACAW/MACAW.h"
#include "../ProtoMon/routing.h"

// maximale Nachrichtenlänge
#define max_msg_len (240 - Routing_Header_len - MAC_Header_len)

// Anzahl Netzwerkteilnehmer
#define anz_knoten 9
#define min_addr 7

// Struktur für den Nachrichtenheader
typedef struct Routing_Header {
	uint8_t ctrl;			// Kontrollflag
	uint8_t src;		// Absenderadresse
	uint8_t dst;		// Zieladresse
	int RSSI;
	uint8_t prev;
	uint16_t len;
} Routing_Header;
#define Routing_Header_len sizeof(uint8_t) + sizeof(uint8_t) +  sizeof(uint8_t) + sizeof(uint16_t)// ctrl, src, dest, len

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


#endif //DIJKSTRA_H
