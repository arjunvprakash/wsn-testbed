#include "metric.h"

#include <stdio.h>  // printf
#include <stdlib.h> // rand, malloc, free, exit
#include <string.h> // memcpy, strerror, strrok

#include "../util.h"

#define ERROR_INVALID_INDEX -1

static void *allocateMemoryForType(Param_Type type);
static int updateValue(Parameter *param, void *value);
static int setValue(Parameter *param, void *value);
static void Metric_init(Metric *metric, uint8_t addr, Parameter *params, uint8_t numParams);

/**
 * Updates the value of a parameter within a metric.
 * 
 * @param metric Pointer to the Metric structure.
 * @param index Index of the parameter to update.
 * @param value Pointer to the new value to be added to the parameter.
 * @return 0 on success, -1 if the metric or index is invalid, -2 if memory allocation fails or type is unsupported.
 */
int Metric_updateParamVal(Metric *metric, uint8_t index, void *value)
{
    if (metric == NULL || index >= metric->numParams || index < 0)
    {
        return ERROR_INVALID_INDEX; // Invalid metric or index
    }

    Parameter *param = &metric->params[index];
    if (param->value == NULL)
    {
        param->value = allocateMemoryForType(param->type);
        if (param->value == NULL)
        {
            logMessage(ERROR, "%s: Error allocating memory for Parameter.value\n", __func__);
            fflush(stdout);
            exit(EXIT_FAILURE);
            // return -2; // Memory allocation failed or unsupported type
        }
    }
    sem_wait(&metric->mutex);
    int result = updateValue(param, value);
    sem_post(&metric->mutex);
    return result;
}

int Metric_setParamVal(Metric *metric, uint8_t index, void *value)
{
    if (metric == NULL || index >= metric->numParams || index < 0)
    {
        return ERROR_INVALID_INDEX; // Invalid metric or index
    }

    Parameter *param = &metric->params[index];
    if (param->value == NULL)
    {
        param->value = allocateMemoryForType(param->type);
        if (param->value == NULL)
        {
            logMessage(ERROR, "%s: Error allocating memory for Parameter.value\n", __func__);
            fflush(stdout);
            exit(EXIT_FAILURE);
            // return -2;
        }
    }
    sem_wait(&metric->mutex);
    int result = setValue(param, value);
    sem_post(&metric->mutex);
    return result;
}

void Metric_initAll(Metric *metrics, uint8_t numMetrics, Parameter *params, uint8_t numParams)
{
    if (metrics == NULL || params == NULL || numMetrics == 0)
    {
        return;
    }

    for (uint8_t i = 0; i < numMetrics; i++)
    {
        Metric_init(&metrics[i], i, params, numParams);
    }
}

static void Metric_init(Metric *metric, uint8_t addr, Parameter *params, uint8_t numParams)
{
    if (metric == NULL || params == NULL)
    {
        return;
    }
    metric->addr = addr;
    metric->numParams = numParams;

    // Copy the params into the metric
    metric->params = malloc(numParams * sizeof(Parameter));
    if (metric->params == NULL)
    {
        printf("Failed to allocate memory for metric parameters\n");
        exit(EXIT_FAILURE);
    }
    for (uint8_t i = 0; i < numParams; i++)
    {
        metric->params[i].name = params[i].name;
        metric->params[i].type = params[i].type;
        metric->params[i].value = NULL;
    }

    sem_init(&metric->mutex, 0, 1);
}

int Metric_getSize(Metric metric)
{
    if (metric.params == NULL)
    {
        return ERROR_INVALID_INDEX; // Invalid metric
    }

    int size = 0;
    for (uint8_t i = 0; i < metric.numParams; i++)
    {
        switch (metric.params[i].type)
        {
        case TYPE_INT:
            size += sizeof(int);
            break;
        case TYPE_FLOAT:
            size += sizeof(float);
            break;
        case TYPE_UINT8:
            size += sizeof(uint8_t);
            break;
        case TYPE_UINT16:
            size += sizeof(uint16_t);
            break;
        case TYPE_INT8:
            size += sizeof(int8_t);
            break;
        case TYPE_INT16:
            size += sizeof(int16_t);
            break;
        default:
            return -2;
        }
    }
    return size;
}

void Metric_reset(Metric *metric)
{
    if (metric == NULL || metric->params == NULL)
    {
        return; // Uninitialzed
    }

    sem_wait(&metric->mutex);
    for (uint8_t i = 0; i < metric->numParams; i++)
    {
        if (metric->params[i].value != NULL)
        {
            switch (metric->params[i].type)
            {
            case TYPE_INT:
                *((int *)metric->params[i].value) = 0;
                break;
            case TYPE_FLOAT:
                *((float *)metric->params[i].value) = 0;
                break;
            case TYPE_UINT8:
                *((uint8_t *)metric->params[i].value) = 0;
                break;
            case TYPE_UINT16:
                *((uint16_t *)metric->params[i].value) = 0;
                break;
            case TYPE_INT8:
                *((int8_t *)metric->params[i].value) = 0;
                break;
            case TYPE_INT16:
                *((int16_t *)metric->params[i].value) = 0;
                break;
            default:
                break;
            }
        }
    }
    sem_post(&metric->mutex);
}

// static void *allocateMemoryForType(Param_Type type)
// {
//     switch (type)
//     {
//     case TYPE_INT:
//         return malloc(sizeof(int));
//     case TYPE_FLOAT:
//         return malloc(sizeof(float));
//     case TYPE_UINT8:
//         return malloc(sizeof(uint8_t));
//     case TYPE_UINT16:
//         return malloc(sizeof(uint16_t));
//     case TYPE_INT8:
//         return malloc(sizeof(int8_t));
//     case TYPE_INT16:
//         return malloc(sizeof(int16_t));
//     default:
//         return NULL;
//     }
// }

static void *allocateMemoryForType(Param_Type type)
{
    void *memory = NULL;
    switch (type)
    {
    case TYPE_INT:
        memory = malloc(sizeof(int));
        break;
    case TYPE_FLOAT:
        memory = malloc(sizeof(float));
        break;
    case TYPE_UINT8:
        memory = malloc(sizeof(uint8_t));
        break;
    case TYPE_UINT16:
        memory = malloc(sizeof(uint16_t));
        break;
    case TYPE_INT8:
        memory = malloc(sizeof(int8_t));
        break;
    case TYPE_INT16:
        memory = malloc(sizeof(int16_t));
        break;
    default:
        return NULL;
    }
    if (memory != NULL)
    {
        memset(memory, 0, sizeof(memory)); // Initialize memory to zero
    }
    return memory;
}

static int updateValue(Parameter *param, void *value)
{
    switch (param->type)
    {
    case TYPE_INT:
        *((int *)param->value) += *((int *)value);
        break;
    case TYPE_FLOAT:
        *((float *)param->value) += *((float *)value);
        break;
    case TYPE_UINT8:
        *((uint8_t *)param->value) += *((uint8_t *)value);
        break;
    case TYPE_UINT16:
        *((uint16_t *)param->value) += *((uint16_t *)value);
        break;
    case TYPE_INT8:
        *((int8_t *)param->value) += *((int8_t *)value);
        break;
    case TYPE_INT16:
        *((int16_t *)param->value) += *((int16_t *)value);
        break;
    default:
        return -2;
    }
    return 0;
}

static int setValue(Parameter *param, void *value)
{
    switch (param->type)
    {
    case TYPE_INT:
        *((int *)param->value) = *((int *)value);
        break;
    case TYPE_FLOAT:
        *((float *)param->value) = *((float *)value);
        break;
    case TYPE_UINT8:
        *((uint8_t *)param->value) = *((uint8_t *)value);
        break;
    case TYPE_UINT16:
        *((uint16_t *)param->value) = *((uint16_t *)value);
        break;
    case TYPE_INT8:
        *((int8_t *)param->value) = *((int8_t *)value);
        break;
    case TYPE_INT16:
        *((int16_t *)param->value) = *((int16_t *)value);
        break;
    default:
        return -2;
    }
    return 0;
}
