#include "ALOHA.h"

#include <errno.h>	   // errno
#include <pthread.h>   // pthread_create
#include <semaphore.h> // sem_init, sem_wait, sem_trywait, sem_timedwait
#include <stdbool.h>   // bool, true, false
#include <stdio.h>	   // printf
#include <stdlib.h>	   // rand, malloc, free, exit
#include <string.h>	   // memcpy, strerror

#include "routing.h"
#include "../common.h"


