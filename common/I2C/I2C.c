#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "I2C.h"

#define I2C_SLAVE 0x0703

int i2c_open(int devID) {
    int fd = open("/dev/i2c-1", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Error %d opening /dev/i2c-1: %s\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (ioctl(fd, I2C_SLAVE, devID) < 0) {
		fprintf(stderr, "Error %d selecting Device 0x77: %s\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

    return fd;
}

int i2c_close(int fd) {
    return close(fd);
}

char i2c_read(int fd, char addr) {
    write(fd, &addr, 1);

    char b;
    read(fd, &b, 1);

    return b;
}

int i2c_read_block(int fd, char addr, char* buffer, int length) {
    write(fd, &addr, 1);
    return read(fd, buffer, length);
}

int i2c_write(int fd, char addr, char data) {
    char buff[2];
    buff[0] = addr;
    buff[1] = data;

    return write(fd, buff, 2);
}
