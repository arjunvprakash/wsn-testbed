#ifndef MACAW_H
#define MACAW_H

#include <stdint.h>			// uint8_t, int8_t, uint32_t

#include "../common.h"
#include "../ProtoMon/mac.h"

// Struktur für den Nachrichtenheader
typedef struct MAC_Header {
	uint8_t ctrl;			// Kontrollflag
	uint8_t src_addr;		// Absenderadresse
	uint8_t dst_addr;		// Zieladresse
	uint16_t seq;			// Sequenznummer
	uint16_t msg_len;		// Nachrichtenlänge
	uint8_t checksum;		// Checksumme
} MAC_Header;
#define MAC_Header_len 8

// Struktur für die Daten des MACAW-Protokolls
typedef struct MAC {
	/* konfigurierbare Parameter */
	uint8_t addr;				// Adresse des Pis
	unsigned int maxtrials;		// Anzahl Sendeversuche
	int noiseThreshold;			// Schwellwert für das Ambient Noise
	unsigned int recvTimeout;	// Timeout beim Empfangen der Bytes einer Nachricht in Millisekunden
	unsigned int timeout;		// Timeout in Sekunden für das Warten auf Bestätigungen
	unsigned int timeslot;		// Timeslot in Millisekunden für den Backoff
	unsigned int t_offset;		// Offset für das Versenden von Bytes in Millisekunden
	unsigned int t_perByte;		// Übertragungsdauer für jedes zusätzliche Byte in Millisekuden

	/* Daten zur letzten empfangenen Nachricht */
	MAC_Header recvH;			// Nachrichtenheader der letzten empfangenen Nachricht
	int RSSI;					// RSSI-Wert der letzten empfangenen Nachricht

	/* Sonstige */
	int debug;                  // Gibt an, ob Debug-Ausgaben erstellt werden sollen

	// Enable ambient noise monitoring
	int ambient;
} MAC;

void MACAW_init(MAC*, unsigned char);

int MACAW_recv(MAC*, unsigned char*);
int MACAW_tryrecv(MAC*, unsigned char*);
int MACAW_timedrecv(MAC*, unsigned char*, unsigned int);

int MACAW_send(MAC*, unsigned char, unsigned char*, unsigned int);
int MACAW_Isend(MAC*, unsigned char, unsigned char*, unsigned int);


#endif /* MACAW_H */
