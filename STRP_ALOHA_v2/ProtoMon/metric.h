#ifndef METRIC_H
#define METRIC_H
#pragma once

#include <stdint.h>
#include <semaphore.h> // sem_init, sem_wait, sem_trywait, sem_timedwait

#include "../common.h"


typedef enum Param_Type
{
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_UINT8,
    TYPE_UINT16,
    TYPE_INT8,
    TYPE_INT16
} Param_Type;

typedef struct Parameter
{
    char *name;
    Param_Type type;
    void *value;
} Parameter;

typedef struct Metric
{
    Parameter *params; // Array of parameters
    uint8_t numParams; // Number of parameters in use
    uint8_t addr;
    sem_t mutex; // Mutex for thread safety
} Metric;


int Metric_setParamVal(Metric *metric, uint8_t index, void *value);
int Metric_getSize(Metric metric);
void Metric_reset(Metric *metric);
int Metric_updateParamVal(Metric *metric, uint8_t index, void *increment);
void Metric_initAll(Metric *metrics, uint8_t numMetrics, Parameter *params, uint8_t numParams);

#endif // METRIC_H