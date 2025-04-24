#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h> 
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "GPIO.h"

#define BLOCK_SIZE (4 * 1024)

#define GPIO_PERI_BASE_2835 0x3F000000
#define GPIO_BASE           (GPIO_PERI_BASE_2835 + 0x200000)

static volatile unsigned int* gpio;

void GPIO_init() {
    int fd = open("/dev/gpiomem", O_RDWR | O_SYNC | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "Error %d opening /dev/gpiomem: %s\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    gpio = mmap(0, BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, GPIO_BASE);
    if (gpio == MAP_FAILED) {
        fprintf(stderr, "Error %d from mmap: %s\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void GPIO_setup(int pin, int mode) {
    if (mode == GPIO_IN)
        *(gpio + pin / 10) = *(gpio + pin / 10) & ~(7 << (pin % 10) * 3);
    else if (mode == GPIO_OUT)
        *(gpio + pin / 10) = *(gpio + pin / 10) & ~(7 << (pin % 10) * 3) | (1 << (pin % 10) * 3);
}

int GPIO_input(int pin) {
    return (*(gpio + 13) & (1 << (pin & 31))) != 0 ? GPIO_HIGH : GPIO_LOW;
}

void GPIO_output(int pin, int state) {
    *(gpio + (state == GPIO_LOW ? 10 : 7)) = 1 << (pin & 31);
}
