#ifndef SX1262_H
#define SX1262_H

#define SX1262_Transmission  0
#define SX1262_DeepSleep     1
#define SX1262_Configuration 2

unsigned int msleep(unsigned int);

void SX1262_init(unsigned int, int);
void SX1262_setMode(int);

void SX1262_recv(unsigned char*, unsigned int);
unsigned int SX1262_tryrecv(unsigned char*, unsigned int);
unsigned int SX1262_timedrecv(unsigned char*, unsigned int, unsigned int);

void SX1262_send(unsigned char*, unsigned int);

#endif // SX1262_H
