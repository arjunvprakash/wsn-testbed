#ifndef ROUTING_H
#define ROUTING_H

#include "../ALOHA/ALOHA.h"

// maximale Nachrichtenlänge
#define max_msg_len (240 - Routing_Header_len - MAC_Header_len)

// Anzahl Netzwerkteilnehmer
#define anz_knoten 7

// Struktur für den Nachrichtenheader
typedef struct Routing_Header {
	uint8_t ctrl;			// Kontrollflag
	uint8_t src_addr;		// Absenderadresse
	uint8_t dst_addr;		// Zieladresse
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

void Routing_init(Routing*, unsigned char);

int Routing_recv(Routing*, unsigned char*);
int Routing_tryrecv(Routing*, unsigned char*);
int Routing_timedrecv(Routing*, unsigned char*, unsigned int);

int Routing_send(Routing*, unsigned char, unsigned char*, unsigned int);
int Routing_Isend(Routing*, unsigned char, unsigned char*, unsigned int);

#endif
