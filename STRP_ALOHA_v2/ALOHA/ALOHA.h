#ifndef ALOHA_H
#define ALOHA_H
#pragma once

#include <stdint.h> // uint8_t, int8_t, uint32_t
// #include "../ProtoMon/mac.h"
#include "../ProtoMon/macV2.h"

#define ADDR_BROADCAST 0XFF

// Struktur für den Nachrichtenheader
typedef struct MAC_Header
{
	uint8_t ctrl;	  // Kontrollflag
	uint8_t src_addr; // Absenderadresse
	uint8_t dst_addr; // Zieladresse
	uint16_t seq;	  // Sequenznummer
	uint16_t msg_len; // Nachrichtenlänge
	uint8_t checksum; // Checksumme
} MAC_Header;
#define MAC_Header_len 8

// Struktur für die Daten des MAC-Protokolls
typedef struct MAC
{
	/* konfigurierbare Parameter */
	uint8_t addr;			  // Adresse des Pis
	unsigned int maxtrials;	  // Anzahl Sendeversuche
	int noiseThreshold;		  // Schwellwert für das Ambient Noise
	unsigned int recvTimeout; // Timeout beim Empfangen der Bytes einer Nachricht in Millisekunden

	/* Daten zur letzten empfangenen Nachricht */
	MAC_Header recvH; // Nachrichtenheader der letzten empfangenen Nachricht
	int RSSI;		  // RSSI-Wert der letzten empfangenen Nachricht

	/* Sonstige */
	int debug; // Gibt an, ob Debug-Ausgaben erstellt werden sollen

	// Enable ambient noise monitoring
	int ambient;

	// Backoff time in ms if noise too high
	int noiseBackoffMs;
} MAC;

void ALOHA_init(MAC *, unsigned char);

int ALOHA_recv(MAC *, unsigned char *);
int ALOHA_tryrecv(MAC *, unsigned char *);
int ALOHA_timedrecv(MAC *, unsigned char *, unsigned int);

int ALOHA_send(MAC *, unsigned char, unsigned char *, unsigned int);
int ALOHA_Isend(MAC *, unsigned char, unsigned char *, unsigned int);


#endif /* ALOHA_H */
