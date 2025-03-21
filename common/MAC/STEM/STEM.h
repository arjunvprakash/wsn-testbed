#ifndef STEM_H
#define STEM_H

#include <stdint.h>			// uint8_t, int8_t, uint32_t

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
	unsigned int t_sleep;		// Sleep-Time in Sekunden
	unsigned int t_wake;		// Wake-Time in Sekunden
	unsigned int t_beacon;		// Beacon-Sendedauer in Sekunden
	unsigned int T_beacon;		// Beacon-Frequenz in Sekunden

	/* Daten zur letzten empfangenen Nachricht */
	MAC_Header recvH;			// Nachrichtenheader der letzten empfangenen Nachricht
	int RSSI;					// RSSI-Wert der letzten empfangenen Nachricht

	/* Sonstige */
	int debug;                  // Gibt an, ob Debug-Ausgaben erstellt werden sollen
} MAC;

void MAC_init(MAC*, unsigned char);

int MAC_recv(MAC*, unsigned char*);
int MAC_tryrecv(MAC*, unsigned char*);
int MAC_timedrecv(MAC*, unsigned char*, unsigned int);

int MAC_send(MAC*, unsigned char, unsigned char*, unsigned int);
int MAC_Isend(MAC*, unsigned char, unsigned char*, unsigned int);

#endif /* STEM_H */
