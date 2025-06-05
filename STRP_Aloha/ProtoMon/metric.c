#include "metric.h"

#include <stdio.h>     // printf
#include <stdlib.h>    // rand, malloc, free, exit
#include <string.h>    // memcpy, strerror, strrok
#include <semaphore.h> // sem_init, sem_wait, sem_trywait, sem_timedwait

int Metric_setParamVal(Metric *metric, uint8_t index, void *value)
{
    if (index >= metric->numParams)
    {
        return -1; // Invalid parameter index
    }

    Parameter param = metric->params[index];
    if (param.value == NULL)
    {
        switch (param.type)
        {
        case TYPE_INT:
            param.value = malloc(sizeof(int));
            break;
        case TYPE_FLOAT:
            param.value = malloc(sizeof(float));
            break;
        case TYPE_UINT8:
            param.value = malloc(sizeof(uint8_t));
            break;
        case TYPE_UINT16:
            param.value = malloc(sizeof(uint16_t));
            break;
        case TYPE_INT8:
            param.value = malloc(sizeof(int8_t));
            break;
        case TYPE_INT16:
            param.value = malloc(sizeof(int16_t));
            break;
        default:
            return -2; // Unsupported parameter type
        }
    }

    if (param.type == TYPE_INT)
    {
        *((int *)metric->params[index].value) = *((int *)value);
    }
    else if (param.type == TYPE_FLOAT)
    {
        *((float *)metric->params[index].value) = *((float *)value);
    }
    else if (param.type == TYPE_UINT8)
    {
        *((uint8_t *)metric->params[index].value) = *((uint8_t *)value);
    }
    else if (param.type == TYPE_UINT16)
    {
        *((uint16_t *)metric->params[index].value) = *((uint16_t *)value);
    }
    else if (param.type == TYPE_INT8)
    {
        *((int8_t *)metric->params[index].value) = *((int8_t *)value);
    }
    else if (param.type == TYPE_INT16)
    {
        *((int16_t *)metric->params[index].value) = *((int16_t *)value);
    }
    else
    {
        return -2; // Unsupported parameter type
    }

    return 0; // Success
}

void Metric_init(Metric *metric, uint8_t addr, Parameter *params, uint8_t numParams)
{
    if (metric == NULL || params == NULL)
    {
        // Handle error: invalid pointers
        return;
    }
    metric->addr = addr;
    metric->numParams = numParams;
    for (uint8_t i = 0; i < numParams; i++)
    {
        // metric->params[i] = malloc(sizeof(Parameter));
        metric->params[i] = params[i];
        // memcpy(metric->params[i], template, sizeof(Parameter) * numParams);
        // Parameter *param = &metric->params[i];
        // param->name = template.name;
        // param->type = template.type;
    }
}
