#include "SX1262.h"

#include <errno.h>          // errno
#include <fcntl.h>          // open
#include <pthread.h>        // pthread_create
#include <semaphore.h>      // sem_init, sem_wait, sem_trywait, sem_timedwait
#include <stdint.h>         // uint8_t, int8_t, uint32_t
#include <stdio.h>          // printf
#include <stdlib.h>         // rand
#include <string.h>         // strerror
#include <termios.h>        // tcgetattr, tcsetattr, tcflush
#include <unistd.h>         // sleep, read, write, close

#include "../GPIO/GPIO.h"   // GPIO_init, GPIO_setup, GPIO_input, GPIO_output

#define M0 22
#define M1 27

// Broadcast -> 0xFFFF
#define ADDH '\xFF'
#define ADDL '\xFF'

#define NETID '\x00'

// baud rate, parity bit, wireless air speed
// #define REG0 '\x62'

// Reduce
#define REG0 '\x67'

// dividing packet, ambient noise, transmit power
// #define REG1 '\x32'

// Lower transmit power
#define REG1 '\x30'


// channel control 0 - 83, 850.125 + REG2 * 1MHz
#define REG2 '\x12'

// RSSI byte, transmitting mode, relay, LBT, WOR -mode, -period
#define REG3 '\x83'

#define CRYPT_H '\x00'
#define CRYPT_L '\x00'

// Struktur für eine Byte-Warteschlange
#define recvQ_size 256
typedef struct recvQueue {
    uint8_t data[recvQ_size];        // Daten bzw. Bytes der Warteschlange
    unsigned int begin, end;         // Zeiger auf den Anfang und das Ende der Daten
    sem_t mutex, free, full;         // Semaphoren 
} recvQueue;

// Struktur zum Speichern mehrere Bytes
typedef struct Bytes {
    uint8_t* bytes;           // Zeiger auf die zu speichernden Bytes
    unsigned int count;       // Anzahl der gespeicherten Bytes
} Bytes;

// Struktur eine Warteschlange mit Elementen bestehend aus mehreren Bytes
#define sendQ_size 256
typedef struct sendQueue {
    Bytes data[sendQ_size];         // Bytes der Warteschlange
    unsigned int begin, end;        // Zeiger auf den Anfang und das Ende
    sem_t mutex, free, full;        // Semaphoren
} sendQueue;

// File Descriptor der seriellen Schnittstelle
static int ser;

// Empfangsthread
static pthread_t recvT;

// Sendethread
static pthread_t sendT;

// Empfangswarteschlange
static recvQueue recvQ;

// Sendewarteschlange
static sendQueue sendQ;

static void recvQ_init() {
    // Start- und Endzeiger initialisieren
    recvQ.begin = 0;
    recvQ.end = 0;

    // Semaphoren initialisieren
    sem_init(&recvQ.mutex, 0, 1);
    sem_init(&recvQ.free, 0, recvQ_size);
    sem_init(&recvQ.full, 0, 0);
}

static void recvQ_enqueue(uint8_t c) {
    // ggf. blockieren und Semaphoren dekrementieren
    sem_wait(&recvQ.free);
    sem_wait(&recvQ.mutex);

    // Byte in der Warteschlange speichern und Endzeiger inkrementieren
    recvQ.data[recvQ.end] = c;
    recvQ.end = (recvQ.end + 1) % recvQ_size;

    // Semaphoren inkrementieren
    sem_post(&recvQ.mutex);
    sem_post(&recvQ.full);
}

static uint8_t recvQ_dequeue() {
    // ggf. blockieren und Semaphoren dekrementieren
    sem_wait(&recvQ.full);
    sem_wait(&recvQ.mutex);

    // Byte aus der Warteschlange speichern und Startzeiger inkrementieren
    uint8_t c = recvQ.data[recvQ.begin];
    recvQ.begin = (recvQ.begin + 1) % recvQ_size;

    // Semaphoren inkrementieren
    sem_post(&recvQ.mutex);
    sem_post(&recvQ.free);

    // Byte zurückgeben
    return c;
}

static int recvQ_trydequeue(uint8_t* c) {
    // ggf. blockieren und Semaphoren dekrementieren
    if (sem_trywait(&recvQ.full) == -1)
        return 0;

    sem_wait(&recvQ.mutex);

    // Byte aus der Warteschlange speichern und Startzeiger inkrementieren
    *c = recvQ.data[recvQ.begin];
    recvQ.begin = (recvQ.begin + 1) % recvQ_size;

    // Semaphoren inkrementieren
    sem_post(&recvQ.mutex);
    sem_post(&recvQ.free);

    // Byte zurückgeben
    return 1;
}

static int recvQ_timeddequeue(uint8_t* c, struct timespec* ts) {
	// ggf. blockieren und Semaphoren dekrementieren, bei Timeout 0 zurückgeben
	if (sem_timedwait(&recvQ.full, ts) == -1)
		return 0;

	sem_wait(&recvQ.mutex);

	// Nachricht aus der Warteschlange speichern und Startzeiger inkrementieren
    *c = recvQ.data[recvQ.begin];
    recvQ.begin = (recvQ.begin + 1) % recvQ_size;

	// Semaphoren inkrementieren
	sem_post(&recvQ.mutex);
    sem_post(&recvQ.free);

    return 1;
}

static void sendQ_init() {
    // Start- und Endzeiger initialisieren
    sendQ.begin = 0;
    sendQ.end = 0;

    // Semaphoren initialisieren
    sem_init(&sendQ.mutex, 0, 1);
    sem_init(&sendQ.free, 0, recvQ_size);
    sem_init(&sendQ.full, 0, 0);
}

static void sendQ_enqueue(Bytes c) {
    // ggf. blockieren und Semaphoren dekrementieren
    sem_wait(&sendQ.free);
    sem_wait(&sendQ.mutex);

    // Byte-Struktur in der Warteschlange speichern und Endzeiger inkrementieren
    sendQ.data[sendQ.end] = c;
    sendQ.end = (sendQ.end + 1) % sendQ_size;

    // Semaphoren inkrementieren
    sem_post(&sendQ.mutex);
    sem_post(&sendQ.full);
}

static Bytes sendQ_dequeue() {
    // ggf. blockieren und Semaphoren dekrementieren
    sem_wait(&sendQ.full);
    sem_wait(&sendQ.mutex);

    // Byte-Struktur der Warteschlange speichern und Startzeiger inkrementieren
    Bytes c = sendQ.data[sendQ.begin];
    sendQ.begin = (sendQ.begin + 1) % sendQ_size;

    // Semaphoren inkrementieren
    sem_post(&sendQ.mutex);
    sem_post(&sendQ.free);

    // Byte zurückgeben
    return c;
}

static void* recvBytes_func(void* args) {
    while (1) {
        uint8_t c;

        // Blockieren bis 1 Byte gelesen wird
        read(ser, &c, 1);

        // gelesenes Byte zur Warteschlange hinzufügen
        recvQ_enqueue(c);
    }
}

static void* sendBytes_func(void* args) {
    while (1) {
        // Blockieren bis Bytes in der Warteschlange verfügbar ist
        Bytes c = sendQ_dequeue();

        // Bytes aus der Warteschlange auf die ser. Schnittstelle schreiben
        write(ser, c.bytes, c.count);

        // allokierten Speicher freigeben
        free(c.bytes);
    }
}

unsigned int msleep(unsigned int ms) {
    struct timespec req, rem;
    req.tv_sec  =  ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000;

    nanosleep(&req, &rem);

    return rem.tv_sec * 1000 + rem.tv_nsec / 1000000;
}

void SX1262_init(unsigned int channel, int mode) {
    /*** GPIO-Pins ***/
    // GPIO Pins initialisieren
	GPIO_init();
	GPIO_setup(M0, GPIO_OUT);
	GPIO_setup(M1, GPIO_OUT);

	// Konfigurationsmodus einstellen
	GPIO_output(M0, GPIO_LOW);
	GPIO_output(M1, GPIO_HIGH);
	msleep(1000);
    /*** GPIO-Pins ***/

    /*** serielle Schnittstelle ***/
	// serielle Schnittstelle öffnen
	ser = open("/dev/ttyS0", O_RDWR | O_NOCTTY | O_SYNC);       // Read & Write | Will not become the process's controlling terminal | Write operations on the file will complete synchronized
    if (ser < 0) {
        fprintf(stderr, "Error %d opening /dev/ttyS0: %s\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Datenstruktur für die Attribute der ser. Schnittstelle
    struct termios options;

    // aktuelle Attribute in der Datenstruktur speichern
    if (tcgetattr(ser, &options) != 0) {
        fprintf(stderr, "Error %d from tcgetattr: %s\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Attribute auf einen "Rohzustand" setzen
    cfmakeraw(&options);

    // Baudrate auf 9600 setzen
    cfsetospeed(&options, B9600);
    cfsetispeed(&options, B9600);

    // Blockierendes Lesen ohne Timeout
    options.c_cc[VMIN]  = 1;                // Blockieren bis 1 Byte verfügbar ist
    options.c_cc[VTIME] = 0;                // Kein Timeout

    // Attribute setzen
    if (tcsetattr(ser, TCSANOW, &options) != 0) {
        fprintf(stderr, "Error %d from tcsetattr: %s\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
    /*** serielle Schnittstelle ***/
	
    /*** Konfiguration ***/
	// Puffer zurücksetzen
	tcflush(ser, TCIOFLUSH);

	// Konfiguration auf den Bus schreiben
	const uint8_t CFG_REG[] = { '\xC2', '\x00', '\x09', ADDH, ADDL, NETID, REG0, REG1, channel - 850, REG3, CRYPT_H, CRYPT_L };
	write(ser, CFG_REG, sizeof(CFG_REG));

    // 100ms warten, während die Konfiguration übernommen wird
    msleep(100);

    // Antwort des Moduls lesen und speichern
    uint8_t RET_REG[sizeof(CFG_REG)];
    read(ser, RET_REG, sizeof(RET_REG));

    // Falsche Antwort vom Modul
    if (RET_REG[0] != '\xC1') {
        fprintf(stderr, "Fehler: Konfiguration konnte nicht übernommen werden:");
        for (int j = 0; j < sizeof(RET_REG); j++)
            fprintf(stderr, " %02X", RET_REG[j]);
        fprintf(stderr, "\n");

        exit(EXIT_FAILURE);
    }

    // wenn ein Byte der Antwort unterschiedlich -> falsche Konfiguration
    for (int i = 1; i < sizeof(RET_REG); i++) {
        if (RET_REG[i] != CFG_REG[i]) {
            fprintf(stderr, "Fehler: Konfiguration konnte nicht übernommen werden:");
            for (int j = 0; j < sizeof(RET_REG); j++)
                fprintf(stderr, " %02X", RET_REG[j]);
            fprintf(stderr, "\n");

            exit(EXIT_FAILURE);
        }
    }
	
	// je nach gewünschten Modus
    switch (mode) {
        case SX1262_DeepSleep:
            GPIO_output(M0, GPIO_HIGH);		    // deep sleep
            msleep(100);
            break;

        case SX1262_Transmission:
            GPIO_output(M1, GPIO_LOW);		    // transmition
            msleep(100);
            break;

        case SX1262_Configuration:
            break;

        default:
            fprintf(stderr, "SX1262_setMode - Error: Wrong mode specified.\n");
            exit(EXIT_FAILURE);
    }
    /*** Konfiguration ***/

    /*** Warteschlangen ***/
    // Empfangs- und Sendewarteschlange initialisieren
    recvQ_init();
    sendQ_init();
    /*** Warteschlangen ***/

    /*** Threads ***/
    // Empfangsthread starten
    if (pthread_create(&recvT, NULL, &recvBytes_func, NULL) != 0) {
        fprintf(stderr, "Error %d creating recvThread: %s\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Sendethread starten
    if (pthread_create(&sendT, NULL, &sendBytes_func, NULL) != 0) {
        fprintf(stderr, "Error %d creating sendThread: %s\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
    /*** Threads ***/
}

void SX1262_setMode(int mode) {
    switch (mode) {
        case SX1262_Transmission:
            GPIO_output(M0, GPIO_LOW);
            GPIO_output(M1, GPIO_LOW);
            msleep(100);
            break;

        case SX1262_DeepSleep:
            GPIO_output(M0, GPIO_HIGH);
            GPIO_output(M1, GPIO_HIGH);
            msleep(100);
            break;

        case SX1262_Configuration:
            GPIO_output(M0, GPIO_LOW);
            GPIO_output(M1, GPIO_HIGH);
            msleep(1000);
            break;
            
        default:
            fprintf(stderr, "SX1262_setMode - Error: Wrong mode specified.\n");
            break;
    }
}

void SX1262_recv(unsigned char* msg, unsigned int len) {
    // len Bytes aus der Warteschlange entfernen und in den Puffer msg schreiben
    for (int i = 0; i < len; i++)
        msg[i] = recvQ_dequeue();
}

unsigned int SX1262_tryrecv(unsigned char* msg, unsigned int len) {
    // len Bytes aus der Warteschlange entfernen und in den Puffer msg schreiben
    for (int i = 0; i < len; i++)
        if (!recvQ_trydequeue(&msg[i]))
            return i;

    return len;
}

unsigned int SX1262_timedrecv(unsigned char* msg, unsigned int len, unsigned int timeout) {
    // Timeout festlegen
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	uint64_t nsec = ts.tv_nsec + (timeout * 1000000);
	ts.tv_sec  += nsec / 1000000000;
	ts.tv_nsec  = nsec % 1000000000;

    // len Bytes aus der Warteschlange entfernen und in den Puffer msg schreiben
    for (int i = 0; i < len; i++)
        if (!recvQ_timeddequeue(&msg[i], &ts))
            return i;

    return len;
}

void SX1262_send(unsigned char* msg, unsigned int len) {
    // Variable zum Speichern mehrerer Bytes
    Bytes c;

    // Speicher für die Bytes allokieren
    c.bytes = (uint8_t*)malloc(len);
    if (c.bytes == NULL) {
        fprintf(stderr, "SX1262_send: malloc error!\n");
        exit(EXIT_FAILURE);
    }

    // Bytes in den allokierten Speicher kopieren
    memcpy(c.bytes, msg, len);

    // Anzahl Bytes speichern
    c.count = len;

    // Bytes zur Warteschlange hinzufügen
    sendQ_enqueue(c);
}
