#include "Dijkstra.h"

#include <errno.h>	   // errno
#include <limits.h>	   // INT_MAX
#include <pthread.h>   // pthread_create
#include <semaphore.h> // sem_init, sem_wait, sem_trywait, sem_timedwait
#include <stdbool.h>   // bool, true, false
#include <stdio.h>	   // printf
#include <stdlib.h>	   // rand, malloc, free, exit
#include <string.h>	   // memcpy, strerror
#include <unistd.h>	   // sleep

int (*Routing_sendMsg)(uint8_t dest, uint8_t *data, unsigned int len) = Dijkstras_send;
int (*Routing_recvMsg)(Routing_Header *h, uint8_t *data) = Dijkstras_recv;
int (*Routing_timedRecvMsg)(Routing_Header *h, uint8_t *data, unsigned int timeout) = Dijkstras_timedrecv;

// Kontrollflag der Routing-Schicht
#define CTRL_ROU '\xD0'

static Routing *routing;

// Struktur einer zu empfangenden Nachricht
typedef struct recvMessage
{
	Routing_Header header; // Nachrichtenheader
	uint16_t len;		   // Nachrichtenlänge
	uint8_t *data;		   // Payload der Nachricht bzw. die eigentliche Nachricht
	int8_t RSSI;		   // RSSI-Wert der Nachricht
} recvMessage;

// Struktur für die Empfangs-Warteschlange
#define recvMsgQ_size 16
typedef struct recvMsgQueue
{
	recvMessage msg[recvMsgQ_size]; // Nachrichten der Warteschlange
	unsigned int begin, end;		// Zeiger auf den Anfang und das Ende der Daten
	sem_t mutex, free, full;		// Semaphoren
} recvMsgQueue;

// Struktur für eine zu sendende Nachricht
typedef struct sendMessage
{
	uint8_t addr;  // Empfängeradresse
	uint16_t len;  // Nachrichtenlänge
	uint8_t *data; // Payload der Nachricht

	bool blocking; // Gibt an, ob der Anwendungsthread blockiert
	bool *success; // Gibt den erfolgreichen Abschluss einer Übertragung an
	sem_t *fin;	   // Signalisiert den Abschluss der Übertragung
} sendMessage;

// Struktur für die Sende-Warteschlange
#define sendMsgQ_size 16
typedef struct sendMsgQueue
{
	sendMessage msg[sendMsgQ_size]; // Nachrichten der Warteschlange
	unsigned int begin, end;		// Zeiger auf den Anfang und das Ende der Daten
	sem_t mutex, free, full;		// Semaphoren
} sendMsgQueue;

// Empfangsthread
static pthread_t recvT;

// Sendethread
static pthread_t sendT;

// Empfangswarteschlange
static recvMsgQueue recvMsgQ;

// Sendewarteschlange
static sendMsgQueue sendMsgQ;

int Dj_recv(Routing *, unsigned char *);
int Dj_tryrecv(Routing *, unsigned char *);
int Dj_timedrecv(Routing *, unsigned char *, unsigned int);

int Dj_send(Routing *, unsigned char, unsigned char *, unsigned int);
int Dj_Isend(Routing *, unsigned char, unsigned char *, unsigned int);

static void recvMsgQ_init()
{
	// Start- und Endzeiger initialisieren
	recvMsgQ.begin = 0;
	recvMsgQ.end = 0;

	// Semaphoren initialisieren
	sem_init(&recvMsgQ.mutex, 0, 1);
	sem_init(&recvMsgQ.free, 0, recvMsgQ_size);
	sem_init(&recvMsgQ.full, 0, 0);
}

static bool recvMsgQ_tryenqueue(recvMessage msg)
{
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

static recvMessage recvMsgQ_dequeue()
{
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

static bool recvMsgQ_trydequeue(recvMessage *msg)
{
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

static bool recvMsgQ_timeddequeue(recvMessage *msg, struct timespec *ts)
{
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

static void sendMsgQ_init()
{
	// Start- und Endzeiger initialisieren
	sendMsgQ.begin = 0;
	sendMsgQ.end = 0;

	// Semaphoren initialisieren
	sem_init(&sendMsgQ.mutex, 0, 1);
	sem_init(&sendMsgQ.free, 0, sendMsgQ_size);
	sem_init(&sendMsgQ.full, 0, 0);
}

static void sendMsgQ_enqueue(sendMessage msg)
{
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

static bool sendMsgQ_tryenqueue(sendMessage msg)
{
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

static sendMessage sendMsgQ_dequeue()
{
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

int paths[anz_knoten][anz_knoten] = {
	// 7  8  9 10 11 12 13 
	  {0, 10, 0, 0, 0, 0, 10}, // Node 7
	  {10, 0, 0, 0, 0, 0, 10},   // Node 8
	  {0, 0, 0, 0, 0, 0, 0},   // Node 9
	  {0, 0, 0, 0, 0, 0, 0},   // Node 10
	  {0, 0, 0, 0, 0, 0, 0},   // Node 11
	  {0, 0, 0, 0, 0, 0, 0},   // Node 12	
	  {10, 10, 0, 0, 0, 0, 0}  // Node 13
  };
  

static int dijkstra(int quelle, int ziel, int *prev)
{
	// Gibt an, welche Knoten zur Umgebung gehören
	bool umgebung[anz_knoten] = {false};

	// Speichert die Länge des Pfads zu den Knoten
	int weg[anz_knoten] = {0};

	// Bei der Quelle starten
	int quellKnoten, zielKnoten = quelle;

	// Quelle in die Umgebung aufnehmen
	umgebung[quelle] = true;

	// Bis man am Ziel angekommen ist
	while (zielKnoten != ziel)
	{
		// kleinsten Weg mit größtmöglichen Weg initialisieren
		int kleinsterWeg = INT_MAX;

		// alle Knoten durchlaufen
		for (int i = 0; i < anz_knoten; i++)
		{
			// wenn Knoten zur Umgebung gehört
			if (umgebung[i])
			{
				// alle Knoten durchlaufen
				for (int j = 0; j < anz_knoten; j++)
				{
					// Wenn eine Kante zwischen i u. j existiert, Weg i zu j plus Weg zu i kleiner als der bisherige kleinste Weg und j noch nicht zur Umgebung gehört
					if (paths[i][j] > 0 && paths[i][j] + weg[i] < kleinsterWeg && !umgebung[j])
					{
						// neuen kleinsten Weg speichern
						kleinsterWeg = paths[i][j];

						// Neuen Quell- und Zielknoten speichern
						quellKnoten = i;
						zielKnoten = j;
					}
				}
			}
		}

		// Wenn kein Weg gefunden werden konnte, exisitiert kein Pfad zwischen quelle und ziel
		if (kleinsterWeg == INT_MAX)
			return -1;

		// neuen Knoten in die Umgebung aufnehmen
		umgebung[zielKnoten] = true;

		// Weg zum Knoten speichern
		weg[zielKnoten] = paths[quellKnoten][zielKnoten] + weg[quellKnoten];

		// Quellknoten zum Zielknoten speichern
		prev[zielKnoten] = quellKnoten;
	}

	// kleinsten Weg zurückgeben
	return weg[zielKnoten];
}

static int nextNode(int quelle, int ziel)
{
	quelle -= min_addr;
	ziel -= min_addr;
	// Wenn das Ziel nicht in der Routing-Tabelle enthalten ist
	if (ziel < 0 || ziel >= anz_knoten)
		// Kein Pfad zwischen Quelle und Ziel existiert
		return -1;

	// kürzesten Pfad zum Ziel speichern
	int prev[anz_knoten];

	// kürzesten Pfad berechnen
	if (dijkstra(quelle, ziel, prev) == -1)
		// Kein Pfad zwischen Quelle und Ziel existiert
		return -1;

	// nächster Knoten = erster Knoten des Pfads
	int next = ziel;
	while (prev[next] != quelle)
		next = prev[next];

	// nächsten Knoten zurückgeben
	return next + min_addr;
}

static void *recvT_func(void *args)
{
	Routing *r = (Routing *)args;
	MAC *mac = &r->mac;
	uint8_t *addr = &mac->addr;

	while (1)
	{
		// Puffer für die Nachricht
		uint8_t buffer[Routing_Header_len + max_msg_len];

		// Zeiger auf den Puffer
		uint8_t *p = buffer;

		// Nachricht empfangen
		int pktSize = MAC_recv(mac, p);
		// int pktSize = MAC_timedRecv(mac, p, 1);

		// MAC-Nachrichtenheader der empfangenen Nachricht speichern
		MAC_Header mac_recvH = mac->recvH;

		// Struktur für den Routing-Nachrichtenheader
		Routing_Header recvH;

		// Kontrollflag speichern
		recvH.ctrl = *p;
		p += sizeof(recvH.ctrl);

		// Kontrollflag unbekannt
		if (recvH.ctrl != CTRL_ROU)
		{
			if (r->debug)
				// ungültiges Kontrollflag ausgeben
				printf("Kontrollflag %02X unbekannt.\n", recvH.ctrl);

			continue;
		}

		// Absenderadresse speichern
		recvH.src = *p;
		p += sizeof(recvH.src);

		// Zieladresse speichern
		recvH.dst = *p;
		p += sizeof(recvH.dst);

		if (r->debug)
		{
			// Empfangenen Header und Nachricht zum Testen ausgeben
			printf("Empfangen: ");
			for (int i = 0; i < Routing_Header_len; i++)
				printf("%02X ", buffer[i]);
			printf("|");
			for (int i = Routing_Header_len; i < mac_recvH.msg_len; i++)
				printf(" %02X", buffer[i]);
			printf("\n");
		}

		// Wenn die Nachricht an diesen Pi adressiert ist
		if (recvH.dst == *addr)
		{
			// Variable für die Nachricht
			recvMessage msg;

			// Header speichern
			msg.header = recvH;

			// Nachrichtenlänge speichern
			// msg.len = mac_recvH.msg_len - Routing_Header_len;
			memcpy(&msg.len, p, sizeof(msg.len));
			p += sizeof(msg.len);
			msg.header.len = msg.len;

			// Speicher für den Nachrichtenpayload allokieren, bei einem Fehler das Programm beenden
			msg.data = (uint8_t *)malloc(msg.len);
			if (msg.data == NULL)
			{
				fprintf(stderr, "malloc error %d in recvT_func: %s\n", errno, strerror(errno));
				exit(EXIT_FAILURE);
			}

			// Nachricht in den allokierten Speicher kopieren
			memcpy(msg.data, p, msg.len);

			// RSSI-Wert speichern
			msg.RSSI = r->mac.RSSI;

			// Nachricht zur Warteschlange hinzufügen
			if (!recvMsgQ_tryenqueue(msg))
			{
				// Warteschlange voll
				if (r->debug)
					printf("recvMsgQ is full.\n");

				free(msg.data);
			}
		}
		else
		{
			// Nächsten Knoten bestimmen
			int next = nextNode(*addr, recvH.dst);

			// Kein Pfad zum Empfänger existiert
			if (next == -1)
			{
				if (r->debug)
					printf("Nachricht kann nicht weitergeleitet werden: Keine Verbindung zu pi%d.\n", recvH.dst);

				continue;
			}

			// Nachricht über den kürzesten Weg weiterleiten
			if (!MAC_send(mac, next, buffer, pktSize))
			{
				// Nachricht konnte nicht versendet werden
				if (r->debug)
					printf("Nachricht kann nicht weitergeleitet werden: MAC-Fehler beim Sendeversuch zu pi%d.\n", next);

				continue;
			}

			// Weiterleitung ausgeben
			if (r->debug)
				printf("Nachricht wurde an pi%d weitergeleitet.\n", next);
		}
	}
}

static void *sendT_func(void *args)
{
	Routing *r = (Routing *)args;
	MAC *mac = &r->mac;
	uint8_t *addr = &mac->addr;

	while (1)
	{
		// Blockieren und Nachricht aus der Warteschlange speichern
		sendMessage msg = sendMsgQ_dequeue();

		// Puffer für den Nachrichtenheader und -payload
		uint8_t buffer[Routing_Header_len + msg.len];

		// Zeiger auf den Puffer setzen
		uint8_t *p = buffer;

		// Kontrollflag in den Puffer schreiben
		*p = CTRL_ROU;
		p += sizeof(uint8_t);

		// Absenderadresse in den Puffer schreiben
		*p = *addr;
		p += sizeof(*addr);

		// Zieladresse in den Puffer schreiben
		*p = msg.addr;
		p += sizeof(msg.addr);

		// Set length
		memcpy(p, &msg.len, sizeof(msg.len));
		p += sizeof(msg.len);

		// Payload in den Puffer kopieren
		memcpy(p, msg.data, msg.len);

		// allokierten Speicher freigeben
		free(msg.data);

		// Erfolg oder Misserfolg der Übertragung speichern
		bool success;

		// Nächsten Knoten bestimmen
		int next = nextNode(*addr, msg.addr);

		// Kein Pfad zum Empfänger existiert
		if (next == -1)
		{
			if (r->debug)
				printf("Nachricht kann nicht gesendet werden: Keine Verbindung zu pi%d.\n", msg.addr);

			success = false;
		}
		else
		{
			// Nächsten Empfänger ausgeben
			printf("Nachricht wird an pi%d gesendet.\n", next);

			// Nachricht versenden und Erfolg der Übertragung speichern
			success = MAC_send(mac, next, buffer, Routing_Header_len + msg.len);

			if (r->debug)
			{
				if (success)
				{
					// Gesendeten Header und Nachricht zum Testen ausgeben
					printf("Gesendet: ");
					for (int i = 0; i < Routing_Header_len; i++)
						printf("%02X ", buffer[i]);
					printf("|");
					for (int i = Routing_Header_len; i < Routing_Header_len + msg.len; i++)
						printf(" %02X", buffer[i]);
					printf("\n");
				}
				else
					// Misserfolg ausgeben
					printf("Nachricht konnte nicht versendet werden: MAC-Fehler beim Sendeversuch zu pi%d.\n", msg.addr);
			}
		}

		// Wenn der empfangende Thread blockiert
		if (msg.blocking)
		{
			// Erfolg der Übertragung setzen
			*msg.success = success;

			// Signalisieren, dass die Operation abgeschlossen wurde
			sem_post(msg.fin);
		}
	}
}

void Dijkstras_init(Routing *r, unsigned char addr)
{
	// Wenn Adresse außerhalb der Routing-Tabelle liegt
	if (addr - min_addr >= anz_knoten)
	{
		fprintf(stderr, "Adresse 0x%02X befindet sich nicht innerhalb der Routing-Tabelle.\n", addr);
		exit(EXIT_FAILURE);
	}

	// Standardmäßig keine Debug Ausgaben erstellen
	r->debug = 0;

	// MAC-Schicht initialisieren
	MACAW_init(&r->mac, addr);

	routing = r;

	// Warteschlange initialisieren
	recvMsgQ_init();
	sendMsgQ_init();

	// Threads starten, bei Fehler Programm beenden
	if (pthread_create(&recvT, NULL, &recvT_func, r) != 0)
	{
		fprintf(stderr, "Error %d creating recvThread: %s\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (pthread_create(&sendT, NULL, &sendT_func, r) != 0)
	{
		fprintf(stderr, "Error %d creating sendThread: %s\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
}

int Dj_recv(Routing *r, unsigned char *msg_buffer)
{
	// Nachricht aus Warteschlange entfernen
	recvMessage msg = recvMsgQ_dequeue();

	// Nachrichtenheader in der Routing-Struktur speichern
	r->recvH = msg.header;

	// Payload der Nachricht in den übergebenen Puffer kopieren
	memcpy(msg_buffer, msg.data, msg.len);

	// allokierten Speicher freigeben
	free(msg.data);

	// RSSI-Wert in der Routing-Struktur speichern
	r->RSSI = msg.RSSI;

	return msg.len;
}

int Dj_tryrecv(Routing *r, unsigned char *msg_buffer)
{
	// Nachricht verfügbar -> aus Warteschlange entfernen, ansonsten 0 zurückgeben
	recvMessage msg;
	if (!recvMsgQ_trydequeue(&msg))
		return 0;

	// Nachrichtenheader in der Routing-Struktur speichern
	r->recvH = msg.header;

	// Payload der Nachricht in den übergebenen Puffer kopieren
	memcpy(msg_buffer, msg.data, msg.len);

	// allokierten Speicher freigeben
	free(msg.data);

	// RSSI-Wert in der Routing-Struktur speichern
	r->RSSI = msg.RSSI;

	return msg.len;
}

int Dj_timedrecv(Routing *r, unsigned char *msg_buffer, unsigned int timeout)
{
	// Timeout festlegen
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += timeout;

	// Nachricht aus Warteschlange entfernen, bei Timeout 0 zurückgeben
	recvMessage msg;
	if (!recvMsgQ_timeddequeue(&msg, &ts))
		return 0;

	// Nachrichtenheader in der Routing-Struktur speichern
	r->recvH = msg.header;

	// Payload der Nachricht in den übergebenen Puffer kopieren
	memcpy(msg_buffer, msg.data, msg.len);

	// allokierten Speicher freigeben
	free(msg.data);

	// RSSI-Wert in der Routing-Struktur speichern
	r->RSSI = msg.RSSI;

	return msg.len;
}

int Dj_send(Routing *r, unsigned char addr, unsigned char *data, unsigned int len)
{
	// Variablen für die Zeiger deklarieren
	bool success;
	sem_t fin;

	// Semaphore initialisieren
	sem_init(&fin, 0, 0);

	// Nachricht setzen
	sendMessage msg;
	msg.addr = addr;
	msg.len = len;

	msg.blocking = true;
	msg.success = &success;
	msg.fin = &fin;

	// Speicher für den Payload der Nachricht allokieren
	msg.data = (uint8_t *)malloc(len);
	if (msg.data == NULL)
	{
		fprintf(stderr, "Dj_send: malloc error!\n");
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

int Dj_Isend(Routing *r, unsigned char addr, unsigned char *data, unsigned int len)
{
	// Nachricht setzen
	sendMessage msg;
	msg.addr = addr;
	msg.len = len;

	msg.blocking = false;

	// Speicher für den Payload der Nachricht allokieren
	msg.data = (uint8_t *)malloc(len);
	if (msg.data == NULL)
	{
		fprintf(stderr, "Routing_send_nonblocking: malloc error!\n");
		exit(EXIT_FAILURE);
	}

	// Nachricht in den allokierten Speicher kopieren
	memcpy(msg.data, data, len);

	// Nachricht in Warteschlange einfügen
	if (!sendMsgQ_tryenqueue(msg))
	{
		// Warteschlange voll
		free(msg.data);

		return 0;
	}

	return 1;
}

int Dijkstras_send(uint8_t dest, uint8_t *data, unsigned int len)
{
	return Dj_send(routing, dest, data, len);
}

int Dijkstras_recv(Routing_Header *h, uint8_t *data)
{
	int ret = Dj_recv(routing, data);
	*h = routing->recvH;
	return ret;
}

int Dijkstras_timedrecv(Routing_Header *h, uint8_t *data, unsigned int timeout)
{

	int ret = Dj_timedrecv(routing, data, timeout);
	*h = routing->recvH;
	return ret;
}

// No metrics
uint8_t Routing_getHeaderSize()
{
	return Routing_Header_len;
}

uint8_t Routing_isDataPkt(uint8_t ctrl)
{
	return ctrl == CTRL_ROU;
}

uint8_t *Routing_getMetricsHeader()
{
	return "";
}
uint8_t *Routing_getTopologyHeader()
{
	return "";
}
int Routing_getMetricsData(uint8_t *buffer, uint8_t addr)
{
	sprintf(buffer, "");
	return 0;
}
int Routing_getTopologyData(char *buffer, uint16_t size)
{
	sprintf(buffer, "");
	return 0;
}