#ifndef METRIC_H
#define METRIC_H
#pragma once

#include <stdint.h>
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
} Metric;

int Metric_setParamVal(Metric *metric, uint8_t index, void *value);
void Metric_init(Metric *metric, uint8_t addr, Parameter *params, uint8_t numParams);

#endif // METRIC_H