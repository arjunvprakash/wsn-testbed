#include "MACAW.h"

#include <errno.h>			// errno
#include <pthread.h>        // pthread_create
#include <semaphore.h>      // sem_init, sem_wait, sem_trywait, sem_timedwait
#include <stdbool.h>		// bool, true, false
#include <stdio.h>			// printf
#include <stdlib.h>			// rand, malloc, free, exit
#include <string.h>			// memcpy, strerror

#include "../SX1262/SX1262.h"

int (*MAC_send)(MAC *h, unsigned char dest, unsigned char *data, unsigned int len) = MACAW_send;
int (*MAC_recv)(MAC *h, unsigned char *data) = MACAW_recv;
int (*MAC_timedRecv)(MAC *h, unsigned char *data, unsigned int timeout) = MACAW_timedrecv;

// Kontrollflags
#define CTRL_RET '\xC1'		// Antwort des Moduls
#define CTRL_RTS '\xC6'		// Request To Send (RTS)
#define CTRL_CTS '\xC7'		// Clear To Send (CTS)
#define CTRL_MSG '\xC8'		// Nachricht
#define CTRL_ACK '\xC9'		// Acknowledgement

// Struktur einer zu empfangenden Nachricht
typedef struct recvMessage {
	MAC_Header header;			// Nachrichtenheader
	uint8_t* data;				// Payload der Nachricht bzw. die eigentliche Nachricht
	int8_t RSSI;				// RSSI-Wert der Nachricht
} recvMessage;

// Struktur für die Empfangs-Warteschlange
#define recvMsgQ_size 16
typedef struct recvMsgQueue {
	recvMessage msg[recvMsgQ_size];			// Nachrichten der Warteschlange
	unsigned int begin, end;				// Zeiger auf den Anfang und das Ende der Daten
    sem_t mutex, free, full;				// Semaphoren
} recvMsgQueue;

// Struktur für eine zu sendende Nachricht
typedef struct sendMessage {
	uint8_t addr;			// Empfängeradresse
	uint16_t len;			// Nachrichtenlänge
	uint8_t* data;			// Payload der Nachricht

	bool blocking;			// Gibt an, ob der Anwendungsthread blockiert
	bool* success;			// Gibt den erfolgreichen Abschluss einer Übertragung an
	sem_t* fin;				// Signalisiert den Abschluss der Übertragung
} sendMessage;

// Struktur für die Sende-Warteschlange
#define sendMsgQ_size 16
typedef struct sendMsgQueue {
	sendMessage msg[sendMsgQ_size];			// Nachrichten der Warteschlange
	unsigned int begin, end;				// Zeiger auf den Anfang und das Ende der Daten
    sem_t mutex, free, full;				// Semaphoren
} sendMsgQueue;

// Struktur für das Ambient Noise
typedef struct AmbientNoise {
	int8_t value;				// Wert des Ambient Noise in dBm
} AmbientNoise;

// Struktur für das Acknowledgement
typedef struct Acknowledgement {
	uint8_t ctrl;				// Kontrollflag
	uint8_t src_addr;			// Absenderadresse
	uint8_t dst_addr;			// Zieladresse
	uint16_t seq;				// Acknowledgementnummer
} Acknowledgement;
#define ACK_len 5

// Struktur für das Clear To Send und deren Semaphoren
typedef struct RequestToSend {
	uint8_t ctrl;				// Kontrollflag
	uint8_t src_addr;			// Absenderadresse
	uint8_t dst_addr;			// Zieladresse
	uint16_t msg_len;			// Nachrichtenlänge
} RequestToSend;
#define RTS_len 5

// Struktur für das Clear To Send und deren Semaphoren
typedef struct ClearToSend {
	uint8_t ctrl;				// Kontrollflag
	uint8_t src_addr;			// Absenderadresse
	uint8_t dst_addr;			// Zieladresse
	uint16_t msg_len;			// Nachrichtenlänge
} ClearToSend;
#define CTS_len 5

// Zustände des Sendethreads
typedef enum sendT_State {
	idle_s, delay_s, listen_s, awaitNoise_s, awaitCTS_s, awaitAck_s, backoff_s
} sendT_State;

// Empfangsthread
static pthread_t recvT;

// Sendethread
static pthread_t sendT;

// Empfangswarteschlange
static recvMsgQueue recvMsgQ;

// Sendewarteschlange
static sendMsgQueue sendMsgQ;

// Ambient Noise speichern
static AmbientNoise noise;

// Acknowledgement speichern
static Acknowledgement ack;

// Request- und Clear To Send speichern
static ClearToSend cts;

// Gibt an, ob das Ambient Noise empfangen wurde
static sem_t sem_noise;

// Gibt an, ob ein Acknowledgement empfangen wurde
static sem_t sem_ack;

// Gibt an, ob ein Clear To Send empfangen wurde
static sem_t sem_cts;

// Semaphore, die einen freien Kanal signalisiert
static sem_t sem_busy;

// aktuellen Zustand des Sendethread speichern
static sendT_State state;

// Zeitpunkt, ab dem wieder übertragen werden kann
//static struct timespec wait;
static struct timespec NAV = { 0 };

// aktuelle empfangene Sequenznummern
static uint16_t recvSeq[256] = { 0 };

// aktuelle gesendete Sequentnummern
static uint16_t sendSeq[256] = { 0 };

static void recvMsgQ_init() {
	// Start- und Endzeiger initialisieren
	recvMsgQ.begin = 0;
    recvMsgQ.end = 0;

	// Semaphoren initialisieren
	sem_init(&recvMsgQ.mutex, 0, 1);
    sem_init(&recvMsgQ.free, 0, recvMsgQ_size);
    sem_init(&recvMsgQ.full, 0, 0);
}

static bool recvMsgQ_tryenqueue(recvMessage msg) {
	// nicht blockieren und Semaphoren dekrementieren
	if (sem_trywait(&recvMsgQ.free) == -1)
		return false;

	sem_wait(&recvMsgQ.mutex);

	// Nachricht in der Warteschlange speichern und Endzeiger inkrementieren
    recvMsgQ.msg[recvMsgQ.end] = msg;
    recvMsgQ.end = (recvMsgQ.end + 1) % recvMsgQ_size;

	// Semaphoren inkrementieren
	sem_post(&recvMsgQ.mutex);
    sem_post(&recvMsgQ.full);

	return true;
}

static recvMessage recvMsgQ_dequeue() {
	// ggf. blockieren und Semaphoren dekrementieren
	sem_wait(&recvMsgQ.full);
	sem_wait(&recvMsgQ.mutex);

	// Nachricht aus der Warteschlange speichern und Startzeiger inkrementieren
    recvMessage msg = recvMsgQ.msg[recvMsgQ.begin];
    recvMsgQ.begin = (recvMsgQ.begin + 1) % recvMsgQ_size;

	// Semaphoren inkrementieren
	sem_post(&recvMsgQ.mutex);
    sem_post(&recvMsgQ.free);

	// Nachricht zurückgeben
    return msg;
}

static bool recvMsgQ_trydequeue(recvMessage* msg) {
	// nicht blockieren und Semaphoren dekrementieren
	if (sem_trywait(&recvMsgQ.full) == -1)
		return false;

	sem_wait(&recvMsgQ.mutex);

	// Nachricht aus der Warteschlange speichern und Startzeiger inkrementieren
    *msg = recvMsgQ.msg[recvMsgQ.begin];
    recvMsgQ.begin = (recvMsgQ.begin + 1) % recvMsgQ_size;

	// Semaphoren inkrementieren
	sem_post(&recvMsgQ.mutex);
    sem_post(&recvMsgQ.free);

    return true;
}

static bool recvMsgQ_timeddequeue(recvMessage* msg, struct timespec* ts) {
	// ggf. blockieren und Semaphoren dekrementieren, bei Timeout 0 zurückgeben
	if (sem_timedwait(&recvMsgQ.full, ts) == -1)
		return false;

	sem_wait(&recvMsgQ.mutex);

	// Nachricht aus der Warteschlange speichern und Startzeiger inkrementieren
    *msg = recvMsgQ.msg[recvMsgQ.begin];
    recvMsgQ.begin = (recvMsgQ.begin + 1) % recvMsgQ_size;

	// Semaphoren inkrementieren
	sem_post(&recvMsgQ.mutex);
    sem_post(&recvMsgQ.free);

    return true;
}

static void sendMsgQ_init() {
	// Start- und Endzeiger initialisieren
	sendMsgQ.begin = 0;
    sendMsgQ.end = 0;

	// Semaphoren initialisieren
	sem_init(&sendMsgQ.mutex, 0, 1);
    sem_init(&sendMsgQ.free, 0, sendMsgQ_size);
    sem_init(&sendMsgQ.full, 0, 0);
}

static void sendMsgQ_enqueue(sendMessage msg) {
	// ggf. blockieren und Semaphoren dekrementieren
	sem_wait(&sendMsgQ.free);
	sem_wait(&sendMsgQ.mutex);

	// Nachricht in der Warteschlange speichern und Endzeiger inkrementieren
    sendMsgQ.msg[sendMsgQ.end] = msg;
    sendMsgQ.end = (sendMsgQ.end + 1) % sendMsgQ_size;

	// Semaphoren inkrementieren
	sem_post(&sendMsgQ.mutex);
    sem_post(&sendMsgQ.full);
}

static bool sendMsgQ_tryenqueue(sendMessage msg) {
	// nicht blockieren und Semaphoren dekrementieren
	if (sem_trywait(&sendMsgQ.free) == -1)
		return false;

	sem_wait(&sendMsgQ.mutex);

	// Nachricht in der Warteschlange speichern und Endzeiger inkrementieren
    sendMsgQ.msg[sendMsgQ.end] = msg;
    sendMsgQ.end = (sendMsgQ.end + 1) % sendMsgQ_size;

	// Semaphoren inkrementieren
	sem_post(&sendMsgQ.mutex);
    sem_post(&sendMsgQ.full);

	return true;
}

static sendMessage sendMsgQ_dequeue() {
	// ggf. blockieren und Semaphoren dekrementieren
	sem_wait(&sendMsgQ.full);
	sem_wait(&sendMsgQ.mutex);

	// Nachricht aus der Warteschlange speichern und Startzeiger inkrementieren
    sendMessage msg = sendMsgQ.msg[sendMsgQ.begin];
    sendMsgQ.begin = (sendMsgQ.begin + 1) % sendMsgQ_size;

	// Semaphoren inkrementieren
	sem_post(&sendMsgQ.mutex);
    sem_post(&sendMsgQ.free);

	// Nachricht zurückgeben
    return msg;
}

static int8_t ambientNoise(MAC* mac) {
	// Kommando zum Abrufen des Ambient Noise
	uint8_t cmd[] = { '\xC0', '\xC1', '\xC2', '\xC3', '\x00', '\x01' };

	while (1) {
		// Kommando senden
		SX1262_send(cmd, sizeof(cmd));

		// 1 Sekunde auf das Ambient Noise warten
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += 1;

		// Wenn empfangen, Ambient Noise zurückgeben
		if (sem_timedwait(&sem_noise, &ts) == 0) {
			if (mac->debug)
				printf("Noise: %hhddBm\n", noise.value);

			return noise.value;
		}

		// Timeout ausgeben
		if (mac->debug)
			printf("Timeout beim Abrufen des Ambient Noise.\n");
	}
}

static void requestToSend(MAC* mac, uint8_t addr, uint16_t msg_len) {
	// Puffer für das Request To Send
	uint8_t buffer[RTS_len];

	// Zeiger auf den Puffer setzen
	uint8_t* p = buffer;

	// Kontrollflag in den Puffer schreiben
	*p = CTRL_RTS;
	p += sizeof(uint8_t);

	// Absenderadresse in den Puffer schreiben, Zeiger weitersetzen
	*p = mac->addr;
	p += sizeof(mac->addr);
	
	// Zieladresse in den Puffer schreiben, Zeiger weitersetzen
	*p = addr;
	p += sizeof(addr);
	
	// Nachrichtenlänge in den Puffer schreiben
	*(uint16_t*)p = msg_len;
	
	// Request To Send versenden
	SX1262_send(buffer, sizeof(buffer));

	if (mac->debug)
		// Gesendetes RTS ausgeben
		printf("Sent RTS to pi%d.\n", addr);
}

static void clearToSend(MAC* mac, uint8_t addr, uint16_t msg_len) {
	// Puffer für das Clear To Send
	uint8_t buffer[CTS_len];

	// Zeiger auf den Puffer setzen
	uint8_t* p = buffer;

	// Kontrollflag in den Puffer schreiben
	*p = CTRL_CTS;
	p += sizeof(uint8_t);

	// Absenderadresse in den Puffer schreiben, Zeiger weitersetzen
	*p = mac->addr;
	p += sizeof(mac->addr);
	
	// Zieladresse in den Puffer schreiben, Zeiger weitersetzen
	*p = addr;
	p += sizeof(addr);
	
	// Nachrichtenlänge in den Puffer schreiben
	*(uint16_t*)p = msg_len;
	
	// Clear To Send versenden
	SX1262_send(buffer, sizeof(buffer));

	if (mac->debug)
		// Gesendetes CTS ausgeben
		printf("Sent CTS to pi%d.\n", addr);
}

static void acknowledgement(MAC* mac, MAC_Header recvH) {
	// Puffer für das Acknowledgement
	uint8_t buffer[ACK_len];

	// Zeiger auf buffer setzen
	uint8_t* p = buffer;
	
	// Kontrollflag in buffer schreiben
	*p = CTRL_ACK;
	p += sizeof(uint8_t);

	// Absenderadresse in buffer schreiben, Zeiger weitersetzen
	*p = mac->addr;
	p += sizeof(mac->addr);
	
	// Zieladresse in buffer schreiben, Zeiger weitersetzen
	*p = recvH.src_addr;
	p += sizeof(recvH.src_addr);
	
	// Sequenznummer in buffer kopieren
	*(uint16_t*)p = recvH.seq;
	
	// Acknowledgement versenden
	SX1262_send(buffer, sizeof(buffer));
}

static bool acknowledged(MAC* mac, uint8_t addr) {
	// Timeout festlegen
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += mac->timeout;

	while (1) {
		// Auf das Acknowledgement warten, bei Timeout abbrechen
		if (sem_timedwait(&sem_ack, &ts) == -1)
			return false;

		// Nachricht ist ein Acknowledgement, wenn Sender der vorherige Empfänger ist
		// und Ack-Nummer mit Seq-Nummer übereinstimmt.
		if (addr == ADDR_BROADCAST || (ack.src_addr == addr && ack.seq == sendSeq[addr]))
			return true;
		else if (mac->debug) {
			printf("Wrong ACK -> Expected: src_addr = %02X, seq = %d\n", addr, sendSeq[addr]);
			printf("             Received: src_addr = %02X, seq = %d\n", ack.src_addr, ack.seq);
		}
	}
}

static void* recvT_func(void* args) {
	MAC* mac = (MAC*)args;

	// Gibt an, ob eine eingehende Übertragung stattfindet
	bool transm = false;

	// Empfangszeitpunkt des RTS
	struct timespec transmTime;

	while (1) {
		// Kontrollflag empfangen
		uint8_t ctrl;
		SX1262_recv(&ctrl, sizeof(ctrl));

		// Antwort des Moduls (Ambient Noise)
		if (ctrl == CTRL_RET) {
			// 3 Bytes der Antwort empfangen und speichern
			uint8_t ambient[3];
			if (SX1262_timedrecv(ambient, sizeof(ambient), mac->recvTimeout) != sizeof(ambient)) {
				if (mac->debug)
					printf("Timeout beim Empfangen des Ambient Noise.\n");
				
				continue;
			}

			// Wenn Anfang = 0 und Länge = 1
			if (ambient[0] == '\x00' && ambient[1] == '\x01') {
				// Wenn sich der Sendethread im Zustand awaitNoise befindet
				if (state == awaitNoise_s) {
					// Ambient Noise speichern
					noise.value = -ambient[2] / 2;

					// Erhalt signalisieren
					sem_post(&sem_noise);
				}
			}
			else if (mac->debug)
				// ungültiges Ambient Noise ausgeben
				printf("Ambient-Noise Antwort ungültig: Anfang = %02X, Länge = %02X, RSSI = %hhu\n", ambient[0], ambient[1], ambient[2]);
		}

		// Request To Send (RTS)
		else if (ctrl == CTRL_RTS) {
			// Puffer für das RTS und den RSSI-Wert
			uint8_t rts_buffer[RTS_len + sizeof(int8_t)];

			// Zeiger auf den Puffer setzen
			uint8_t* p = rts_buffer;

			// Kontrollflag im Puffer speichern
			*p = ctrl;
			p += sizeof(ctrl);

			// RTS und RSSI-Wert empfangen
			if (SX1262_timedrecv(p, sizeof(rts_buffer) - sizeof(ctrl), mac->recvTimeout) != sizeof(rts_buffer) - sizeof(ctrl)) {
				if (mac->debug)
					printf("Timeout beim Empfangen des Request To Send.\n");

				continue;
			}

			// Variable für das RTS deklarieren
			RequestToSend recvRTS;

			// Kontrollflag speichern
			recvRTS.ctrl = ctrl;

			// Absenderadresse speichern
			recvRTS.src_addr = *p;
			p += sizeof(recvRTS.src_addr);

			// Zieladresse speichern
			recvRTS.dst_addr = *p;
			p += sizeof(recvRTS.dst_addr);

			// Nachrichtenlänge speichern
			recvRTS.msg_len = *(uint16_t*)p;

			// aktuelle Zeit abrufen
			struct timespec now;
			clock_gettime(CLOCK_REALTIME, &now);

			// Übertragungsdauer in Millisekunden berechnen
			uint64_t ms = (mac->t_offset +  CTS_len                           * mac->t_perByte) +		// Clear To Send
						  (mac->t_offset + (MAC_Header_len + recvRTS.msg_len) * mac->t_perByte) +		// Nachricht
						  (mac->t_offset +  ACK_len                           * mac->t_perByte);		// Acknowledgement

			if (mac->debug)
				// Empfangenes RTS ausgeben
				printf("RTS: pi%d -> pi%d, NAV = %llums\n", recvRTS.src_addr, recvRTS.dst_addr, ms);

			// Wenn das RTS an diesen Pi adressiert ist
			if (recvRTS.dst_addr == mac->addr) {
				// Wenn nicht schon ein RTS empfangen wurde
				// oder ein Timeout auftritt
				// und keine fremde Übertragung stattfindet
				if ((!transm || now.tv_sec - transmTime.tv_sec >= mac->timeout) &&
					(NAV.tv_sec < now.tv_sec || NAV.tv_sec == now.tv_sec && NAV.tv_nsec < now.tv_nsec)) {
					// Clear To Send (CTS) senden
					clearToSend(mac, recvRTS.src_addr, recvRTS.msg_len);

					// Neu empfangene RTS nicht bestätigen
					transm = true;

					// Empfangszeit speichern
					transmTime = now;
				}
			}

			// Übertragungsdauer auf die aktuelle Zeit aufaddieren
			uint64_t ns = now.tv_nsec + ms * 1000000;
			now.tv_sec += ns / 1000000000;
			now.tv_nsec = ns % 1000000000;

			// Endzeitpunkt der Übertragung speichern
			NAV = now;

			// Wenn sich der Sendethread im Zustand "listen" befindet
			if (state == listen_s)
				// Dem Sendethread signalisieren, dass der Kanal nicht frei ist
				sem_post(&sem_busy);
		}

		// Clear To Send (CTS)
		else if (ctrl == CTRL_CTS) {
			// Puffer für das CTS und den RSSI-Wert
			uint8_t cts_buffer[CTS_len + sizeof(int8_t)];

			// Zeiger auf den Puffer setzen
			uint8_t* p = cts_buffer;

			// Kontrollflag im Puffer speichern
			*p = ctrl;
			p += sizeof(ctrl);

			// CTS und RSSI-Wert empfangen
			if (SX1262_timedrecv(p, sizeof(cts_buffer) - sizeof(ctrl), mac->recvTimeout) != sizeof(cts_buffer) - sizeof(ctrl)) {
				if (mac->debug)
					printf("Timeout beim Empfangen des Clear To Send.\n");

				continue;
			}

			// Variable für das CTS deklarieren
			ClearToSend recvCTS;

			// Kontrollflag speichern
			recvCTS.ctrl = ctrl;

			// Absenderadresse speichern
			recvCTS.src_addr = *p;
			p += sizeof(recvCTS.src_addr);

			// Zieladresse speichern
			recvCTS.dst_addr = *p;
			p += sizeof(recvCTS.dst_addr);

			// Nachrichtenlänge speichern
			recvCTS.msg_len = *(uint16_t*)p;

			if (recvCTS.dst_addr != mac->addr) {
				// Übertragungsdauer in Millisekunden berechnen
				uint64_t ms = (mac->t_offset + (MAC_Header_len + recvCTS.msg_len) * mac->t_perByte) +	// Nachricht
							  (mac->t_offset + ACK_len * mac->t_perByte);								// Acknowledgement

				// aktuelle Zeit abrufen
				struct timespec now;
				clock_gettime(CLOCK_REALTIME, &now);

				// Übertragungsdauer auf die aktuelle Zeit aufaddieren
				uint64_t ns = now.tv_nsec + ms * 1000000;
				now.tv_sec  += ns / 1000000000;
				now.tv_nsec  = ns % 1000000000;

				// Endzeitpunkt der Übertragung speichern
				NAV = now;

				if (mac->debug)
					// Empfangenes CTS ausgeben
					printf("CTS: pi%d -> pi%d, NAV = %llums\n", recvCTS.src_addr, recvCTS.dst_addr, ms);
			}

			if (mac->debug)
				// Empfangenes CTS ausgeben
				printf("CTS: pi%d -> pi%d.\n", recvCTS.src_addr, recvCTS.dst_addr);

			// Wenn sich der Sendethread im Zustand "awaitCTS" befindet, also auf ein CTS wartet
			if (state == awaitCTS_s) {
				// CTS speichern
				cts = recvCTS;

				// Empfang dem Sendethread signalisieren
				sem_post(&sem_cts);
			}
			// Wenn sich der Sendethread im Zustand "listen" befindet
			else if (state == listen_s)
				// Dem Sendethread signalisieren, dass der Kanal nicht frei ist
				sem_post(&sem_busy);
		}

		// Acknowledgement
		else if (ctrl == CTRL_ACK) {
			// Puffer für das Acknowledgement und den RSSI-Wert
			uint8_t ack_buffer[ACK_len + sizeof(int8_t)];

			// Zeiger auf den Puffer setzen
			uint8_t* p = ack_buffer;

			// Kontrollflag im Puffer speichern
			*p = ctrl;
			p += sizeof(ctrl);

			// Acknowledgement und RSSI-Wert empfangen
			if (SX1262_timedrecv(p, sizeof(ack_buffer) - sizeof(ctrl), mac->recvTimeout) != sizeof(ack_buffer) - sizeof(ctrl)) {
				if (mac->debug)
					printf("Timeout beim Empfangen des Acknowledgement.\n");

				continue;
			}

			// Variable für das CTS deklarieren
			Acknowledgement recvACK;

			// Kontrollflag speichern
			recvACK.ctrl = ctrl;

			// Absenderadresse speichern
			recvACK.src_addr = *p;
			p += sizeof(recvACK.src_addr);

			// Zieladresse speichern
			recvACK.dst_addr = *p;
			p += sizeof(recvACK.dst_addr);

			// Sequenznummer speichern
			recvACK.seq = *(uint16_t*)p;

			// Wenn das ACK nicht an diesen Pi adressiert ist
			if (recvACK.dst_addr != mac->addr) {
				if (mac->debug)
					// Empfangenes Acknowledgement ausgeben
					printf("ACK: pi%d -> pi%d.\n", recvACK.src_addr, recvACK.dst_addr);

				continue;
			}

			// Wenn sich der Sendethread im Zustand "Await ACK" befindet
			if (state == awaitAck_s) {
				// Acknowledgement speichern
				ack = recvACK;

				// Erhalt dem Sendethread signalisieren
				sem_post(&sem_ack);
			}
		}

		// Nachricht
		else if (ctrl == CTRL_MSG) {
			// Puffer für den Nachrichtenheader
			uint8_t header_buffer[MAC_Header_len];

			// Zeiger auf den Puffer setzen
			uint8_t* p = header_buffer;

			// Kontrollflag im Puffer speichern
			*p = ctrl;
			p += sizeof(ctrl);

			// Nachrichtenheader empfangen
			if (SX1262_timedrecv(p, sizeof(header_buffer) - sizeof(ctrl), mac->recvTimeout) != sizeof(header_buffer) - sizeof(ctrl)) {
				if (mac->debug)
					printf("Timeout beim Empfangen des Nachrichtenheader.\n");

				continue;
			}

			// Variable für den Nachrichtenheader deklarieren
			MAC_Header recvH;

			// Kontrollflag speichern
			recvH.ctrl = ctrl;

			// Absenderadresse speichern
			recvH.src_addr = *p;
			p += sizeof(recvH.src_addr);

			// Zieladresse speichern
			recvH.dst_addr = *p;
			p += sizeof(recvH.dst_addr);

			// Sequenznummer speichern
			recvH.seq = *(uint16_t*)p;
			p += sizeof(recvH.seq);

			// Nachrichtenlänge speichern
			recvH.msg_len = *(uint16_t*)p;
			p += sizeof(recvH.msg_len);

			// Checksumme speichern
			recvH.checksum = *p;

			// Puffer für den Nachrichtenpayload und den RSSI-Wert
			uint8_t msg_buffer[recvH.msg_len + sizeof(int8_t)];

			// Payload der Nachricht und RSSI-Wert empfangen
			if (SX1262_timedrecv(msg_buffer, sizeof(msg_buffer), mac->recvTimeout) != sizeof(msg_buffer)) {
				if (mac->debug)
					printf("Timeout beim Empfangen des Nachrichtenpayloads.\n");

				continue;
			}

			// Checksumme berechnen
			uint8_t checksum = 0;
			for (int i = 0; i < MAC_Header_len - sizeof(recvH.checksum); i++)
				checksum += header_buffer[i];
			for (int i = 0; i < recvH.msg_len; i++)
				checksum += msg_buffer[i];

			// Checksumme prüfen
			if (checksum != recvH.checksum) {
				if (mac->debug)
					printf("Checksumme 0x%02X ungültig! Expected: 0x%02X.\n", recvH.checksum, checksum);

				continue;
			}

			// Übertragungsdauer in Millisekunden berechnen
			uint64_t ms = (mac->t_offset + ACK_len * mac->t_perByte);		// Acknowledgement

			// aktuelle Zeit abrufen
			struct timespec now;
			clock_gettime(CLOCK_REALTIME, &now);

			// Übertragungsdauer auf die aktuelle Zeit aufaddieren
			uint64_t ns = now.tv_nsec + ms * 1000000;
			now.tv_sec  += ns / 1000000000;
			now.tv_nsec  = ns % 1000000000;

			// Endzeitpunkt der Übertragung speichern
			NAV = now;

			// Wenn Nachricht nicht an diesen Pi adressiert ist
			if (recvH.dst_addr != ADDR_BROADCAST && recvH.dst_addr != mac->addr) {
				// Empfangene Nachricht ausgeben
				printf("MSG: pi%d -> pi%d, NAV = %llums\n", recvH.src_addr, recvH.dst_addr, ms);

				// Wenn sich der Sendethread im Zustand "listen" befindet
				if (state == listen_s)
					// Dem Sendethread signalisieren, dass der Kanal nicht frei ist
					sem_post(&sem_busy);

				continue;
			}

			if (mac->debug) {
				// Empfangenen Header und Nachricht zum Testen ausgeben
				printf("Empfangen: ");
				for (int i = 0; i < MAC_Header_len; i++)
					printf("%02X ", header_buffer[i]);
				printf("|");
				for (int i = 0; i < recvH.msg_len; i++)
					printf(" %02X", msg_buffer[i]);
				printf("\n");
			}

			// Wenn eine Nachricht mit einer kleineren oder gleichen Sequenznummer schon empfangen wurde
			// und Sequenznummer nicht die Startsequenznummer ist
			if (recvH.dst_addr != ADDR_BROADCAST && recvH.seq <= recvSeq[recvH.src_addr] && recvH.seq != 0) {
				if (mac->debug)
					printf("... wurde schon empfangen.\n\n");
			}
			else {
				// Ignore sequece number for broadcasts
				if (recvH.dst_addr != ADDR_BROADCAST)
				{
					// aktuelle Sequenznummer speichern
					recvSeq[recvH.src_addr] = recvH.seq;
				}

				// Variable für die Nachricht
				recvMessage msg;

				// Header speichern
				msg.header = recvH;

				// Speicher für den Nachrichtenpayload allokieren, bei einem Fehler das Programm beenden
				msg.data = (uint8_t*)malloc(recvH.msg_len);
				if (msg.data == NULL) {
					fprintf(stderr, "malloc error %d in recvT_func: %s\n", errno, strerror(errno));
					exit(EXIT_FAILURE);
				}

				// Nachricht in den allokierten Speicher kopieren
				memcpy(msg.data, msg_buffer, recvH.msg_len);

				// RSSI-Wert speichern
				msg.RSSI = msg_buffer[recvH.msg_len];

				// Nachricht zur Warteschlange hinzufügen
				if (!recvMsgQ_tryenqueue(msg)) {
					// Warteschlange voll
					if (mac->debug)
						printf("recvMsgQ is full.\n");
						
					free(msg.data);
				}
			}

			
			// Acknowledgment senden
			// Acknowledgment senden
			if (recvH.dst_addr != ADDR_BROADCAST)
			{
				acknowledgement(mac, recvH);
			}

			// Neu empfangene RTS wieder bestätigen
			transm = false;
		}

		// Kontrollflag unbekannt
		else {
			if (mac->debug)
				// ungültiges Kontrollflag ausgeben
				printf("Kontrollflag %02X unbekannt.\n", ctrl);

			// 100ms warten bis die Daten vollständig empfangen wurden
			msleep(100);

			// Solange Bytes verfügbar sind, diese empfangen und verwerfen
			uint8_t c;
			while (SX1262_tryrecv(&c, 1));
		}
	}
}

static unsigned int backoff(unsigned int timeslot, int c) {
	// Exponential Backoff: k × TimeSlot
	// k = 0...2^c-1
	return msleep((rand() % (1 << c)) * timeslot);
}

static void* sendT_func(void* args) {
	MAC* mac = (MAC*)args;

	while (1) {
		// Blockieren und Nachricht aus der Warteschlange speichern
		sendMessage msg = sendMsgQ_dequeue();

		// Puffer für den Nachrichtenheader und -payload
		uint8_t buffer[MAC_Header_len + msg.len];

		// Zeiger auf buffer setzen
		uint8_t* p = buffer;
		
		// Kontrollflag in buffer schreiben
		*p = CTRL_MSG;
		p += sizeof(uint8_t);

		// Absenderadresse in buffer schreiben, Zeiger weitersetzen
		*p = mac->addr;
		p += sizeof(mac->addr);
		
		// Zieladresse in buffer schreiben, Zeiger weitersetzen
		*p = msg.addr;
		p += sizeof(msg.addr);
		
		// Sequenznummer in buffer kopieren, Zeiger weitersetzen
		*(uint16_t*)p = sendSeq[msg.addr];
		p += sizeof(sendSeq[msg.addr]);

		// Nachrichtenlänge in buffer kopieren, Zeiger weitersetzen
		*(uint16_t*)p = msg.len;
		p += sizeof(msg.len);

		// Checksumme berechnen
		uint8_t checksum = 0;
		for (int i = 0; i < MAC_Header_len - sizeof(checksum); i++)
			checksum += buffer[i];
		for (int i = 0; i < msg.len; i++)
			checksum += msg.data[i];

		// Checksumme in buffer schreiben, Zeiger weitersetzen
		*p = checksum;
		p += sizeof(checksum);

		// Payload in den Puffer kopieren
		memcpy(p, msg.data, msg.len);

		// allokierten Speicher freigeben
		free(msg.data);

		// Erfolg der Übertragung auf false setzen
		bool success = false;

		// Anzahl Versuche speichern
		unsigned int numtrials = 1;

		// aktuelle Zeit abrufen
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);

		// Warten bis laufende fremde Übertragungen abgeschlossen wurden
		while (NAV.tv_sec > ts.tv_sec || NAV.tv_sec == ts.tv_sec && NAV.tv_nsec > ts.tv_nsec) {
			msleep((NAV.tv_sec - ts.tv_sec) * 1e3 + (NAV.tv_nsec - ts.tv_nsec) / 1e6);
			clock_gettime(CLOCK_REALTIME, &ts);
		}

		// In den Zustand "delay" wechseln
		state = delay_s;

		// Random Delay
		msleep(rand() % (mac->timeout * 1000));

		while (1) {
			// In den Zustand "listen" wechseln
			state = listen_s;

			// aktuelle Zeit abrufen
			clock_gettime(CLOCK_REALTIME, &ts);

			// Timeslot aufaddieren
			uint64_t ns = ts.tv_nsec + mac->timeslot * 1000000;
			ts.tv_sec += ns / 1000000000;
			ts.tv_nsec = ns % 1000000000;

			// Auf einen freien Kanal warten
			if (sem_timedwait(&sem_busy, &ts) == 0) {
				if (mac->debug)
					printf("Channel is busy.\n");
				
				// anz_versuche = max_versuche -> Sendeversuch abbrechen
				if (numtrials >= mac->maxtrials)
					break;

				// In den Zustand "Backoff" wechseln
				state = backoff_s;

				// aktuelle Zeit abrufen
				clock_gettime(CLOCK_REALTIME, &ts);

				// Warten bis die fremde Übertragung abgeschlossen wurden
				msleep((NAV.tv_sec - ts.tv_sec) * 1e3 + (NAV.tv_nsec - ts.tv_nsec) / 1e6);

				// Backoff
				backoff(mac->timeslot, numtrials++);

				continue;
			}

			// In den Zustand "Await Noise" wechseln
			state = awaitNoise_s;

			// Wenn Noise zu hoch
			// if (ambientNoise(mac) <= mac->noiseThreshold) {
			// 	if (mac->debug)
			// 		printf("Noise is too high.\n");

			// 	// anz_versuche = max_versuche -> Sendeversuch abbrechen
			// 	if (numtrials >= mac->maxtrials)
			// 		break;

			// 	// In den Zustand "Backoff" wechseln
			// 	state = backoff_s;

			// 	// Backoff
			// 	backoff(mac->timeslot, numtrials++);

			// 	continue;
			// }

			// In den Zustand "Await CTS" wechseln
			state = awaitCTS_s;

			// Request To Send (RTS) senden
			if(msg.addr != ADDR_BROADCAST) {
				requestToSend(mac, msg.addr, msg.len);
			

				// Timeout festlegen
				clock_gettime(CLOCK_REALTIME, &ts);
				ts.tv_sec += mac->timeout;
				
				// Auf Clear To Send (CTS) warten
				if (sem_timedwait(&sem_cts, &ts) == -1) {
					if (mac->debug)
						printf("No CTS received.\n");
					
					// anz_versuche = max_versuche -> Sendeversuch abbrechen
					if (numtrials >= mac->maxtrials)
						break;

					// In den Zustand "Backoff" wechseln
					state = backoff_s;

					// Backoff
					backoff(mac->timeslot, numtrials++);

					continue;
				}

				// Wenn CTS nicht an diesen Pi adressiert
				if (cts.dst_addr != mac->addr) {
					if (mac->debug)
						printf("pi%d received CTS from pi%d.\n", cts.dst_addr, cts.src_addr);
					
					// anz_versuche = max_versuche -> Sendeversuch abbrechen
					if (numtrials >= mac->maxtrials)
						break;

					// In den Zustand "Backoff" wechseln
					state = backoff_s;

					// aktuelle Zeit abrufen
					clock_gettime(CLOCK_REALTIME, &ts);

					// Warten bis die fremde Übertragung abgeschlossen wurden
					msleep((NAV.tv_sec - ts.tv_sec) * 1e3 + (NAV.tv_nsec - ts.tv_nsec) / 1e6);

					// Backoff
					backoff(mac->timeslot, numtrials++);

					continue;
				}
			}

			// In den Zustand "Await ACK" wechseln
			state = awaitAck_s;

			// Nachricht versenden
			SX1262_send(buffer, sizeof(buffer));

			if (mac->debug) {
				// Gesendeten Header und Nachricht zum Testen ausgeben
				printf("Gesendet: ");
				for (int i = 0; i < MAC_Header_len; i++)
					printf("%02X ", buffer[i]);
				printf("|");
				for (int i = MAC_Header_len; i < MAC_Header_len + msg.len; i++)
					printf(" %02X", buffer[i]);
				printf("\n");
			}

			// Auf Acknowledgement warten
			if (msg.addr != ADDR_BROADCAST && !acknowledged(mac, msg.addr)) {
				if (mac->debug)
					printf("No ACK received.\n");
				
				// anz_versuche = max_versuche -> Sendeversuch abbrechen
				if (numtrials >= mac->maxtrials)
					break;

				// In den Zustand "Backoff" wechseln
				state = backoff_s;

				// Backoff
				backoff(mac->timeslot, numtrials++);

				continue;
			}

			// Erfolg der Übertragung setzen
			success = true;

			break;
		}

		// In den Zustand "IDLE" wechseln
		state = idle_s;

		// Sequenznummer inkrementieren
		sendSeq[msg.addr]++;

		if (mac->debug) {
			if (success)
				// Nachricht bestätigt
				printf("Nachricht wurde nach %d Versuch(en) bestätigt.\n", numtrials);
			else
				// Nachricht wurde nicht bestätigt
				printf("Nachricht wurde nach %d Versuch(en) nicht bestätigt.\n", numtrials);
		}

		// Wenn der empfangende (Anwendungs-) Thread blockiert
		if (msg.blocking) {
			// Erfolg der Übertragung setzen
			*msg.success = success;

			// Signalisieren, dass die Operation abgeschlossen wurde
			sem_post(msg.fin);
		}
	}
}

void MACAW_init(MAC* mac, unsigned char addr) {
	// Adresse speichern
	mac->addr = addr;

	// untere Schicht initialisieren
	SX1262_init(868, SX1262_Transmission);

	// Warteschlange initialisieren
	recvMsgQ_init();
	sendMsgQ_init();

	// Semaphoren initialisieren
	sem_init(&sem_noise, 0, 0);
	sem_init(&sem_ack, 0, 0);
	sem_init(&sem_cts, 0, 0);
	sem_init(&sem_busy, 0, 0);

	// Zufallsgenerator initialisieren
	srand(addr * time(NULL));

	// maximal 5 Sendeversuche
	mac->maxtrials = 5;

	// nicht senden wenn Noise >= -95dBm
	mac->noiseThreshold = -95;

	// 1 Sekunde Timeout beim Empfangen der Bytes
	mac->recvTimeout = 1000;

    // Standardmäßig keine Debug Ausgaben erstellen
    mac->debug = 0;

	// 1 Sekunde Timeslot und 5 Sekunden Timeout für blockierende Aufrufe
	mac->timeslot = 1000; mac->timeout = 5;

	// Dauer des Sendeoffsets und je Byte in ms
	mac->t_offset = 170; mac->t_perByte = 6;

	// Zustand des Sendethreads initailisieren (IDLE)
	state = idle_s;

	// Threads starten, bei Fehler Programm beenden
	if (pthread_create(&recvT, NULL, &recvT_func, mac) != 0) {
        fprintf(stderr, "Error %d creating recvThread: %s\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

	if (pthread_create(&sendT, NULL, &sendT_func, mac) != 0) {
        fprintf(stderr, "Error %d creating sendThread: %s\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
}

int MACAW_recv(MAC* mac, unsigned char* msg_buffer) {
	// Nachricht aus Warteschlange entfernen
	recvMessage msg = recvMsgQ_dequeue();

	// Nachrichtenheader in der ALOHA-Struktur speichern
	mac->recvH = msg.header;

	// Payload der Nachricht in den übergebenen Puffer kopieren
	memcpy(msg_buffer, msg.data, msg.header.msg_len);

	// allokierten Speicher freigeben
	free(msg.data);

	// RSSI-Wert in der ALOHA-Struktur speichern
	mac->RSSI = msg.RSSI;

	// Anzahl empfangener Bytes zurückgeben
	return msg.header.msg_len;
}

int MACAW_tryrecv(MAC* mac, unsigned char* msg_buffer) {
	// Nachricht verfügbar -> aus Warteschlange entfernen, ansonsten 0 zurückgeben
	recvMessage msg;
	if (!recvMsgQ_trydequeue(&msg))
		return 0;

	// Nachrichtenheader in der ALOHA-Struktur speichern
	mac->recvH = msg.header;

	// Payload der Nachricht in den übergebenen Puffer kopieren
	memcpy(msg_buffer, msg.data, msg.header.msg_len);

	// allokierten Speicher freigeben
	free(msg.data);

	// RSSI-Wert in der ALOHA-Struktur speichern
	mac->RSSI = msg.RSSI;

	// Anzahl empfangener Bytes zurückgeben
	return msg.header.msg_len;
}

int MACAW_timedrecv(MAC* mac, unsigned char* msg_buffer, unsigned int timeout) {
	// Timeout festlegen
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += timeout;

	// Nachricht aus Warteschlange entfernen, bei Timeout 0 zurückgeben
	recvMessage msg;
	if (!recvMsgQ_timeddequeue(&msg, &ts))
		return 0;

	// Nachrichtenheader in der ALOHA-Struktur speichern
	mac->recvH = msg.header;

	// Payload der Nachricht in den übergebenen Puffer kopieren
	memcpy(msg_buffer, msg.data, msg.header.msg_len);

	// allokierten Speicher freigeben
	free(msg.data);

	// RSSI-Wert in der ALOHA-Struktur speichern
	mac->RSSI = msg.RSSI;

	// Anzahl empfangener Bytes zurückgeben
	return msg.header.msg_len;
}

int MACAW_send(MAC* mac, unsigned char addr, unsigned char* data, unsigned int len) {
	// Variablen für die Zeiger deklarieren
	bool success;
	sem_t fin;

	// Semaphore initialisieren
	sem_init(&fin, 0, 0);

	// Nachricht setzen
	sendMessage msg;
	msg.addr = addr;
	msg.len = len;

	// Blockieren und Zeiger setzen
	msg.blocking = true;
	msg.success = &success;
	msg.fin = &fin;

	// Speicher für den Payload der Nachricht allokieren
	msg.data = (uint8_t*)malloc(len);
	if (msg.data == NULL) {
		fprintf(stderr, "malloc error %d in MAC_send: %s\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	// Nachricht in den allokierten Speicher kopieren
	memcpy(msg.data, data, len);

	// Nachricht in Warteschlange einfügen
	sendMsgQ_enqueue(msg);

	// Blockieren bis die Operation abgeschlossen wurde
	sem_wait(&fin);

	// Speicher der Semaphore freigeben
	sem_destroy(&fin);

	// Ausgang der Operation zurückgeben
	return success;
}

int MACAW_Isend(MAC* mac, unsigned char addr, unsigned char* data, unsigned int len) {
	// Nachricht setzen
	sendMessage msg;
	msg.addr = addr;
	msg.len = len;

	// nicht blockieren
	msg.blocking = false;

	// Speicher für den Payload der Nachricht allokieren
	msg.data = (uint8_t*)malloc(len);
	if (msg.data == NULL) {
		fprintf(stderr, "malloc error %d in MACAW_Isend: %s\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	// Nachricht in den allokierten Speicher kopieren
	memcpy(msg.data, data, len);

	// Nachricht in Warteschlange einfügen
	if (!sendMsgQ_tryenqueue(msg)) {
		// Warteschlange voll
		free(msg.data);

		return false;
	}

	return true;
}

uint8_t MAC_getHeaderSize()
{
	return MAC_Header_len;
}

uint8_t *MAC_getMetricsHeader()
{
	return "";
}

int MAC_getMetricsData(uint8_t *buffer, uint8_t addr)
{
	sprintf(buffer,"");
	return 0;
}