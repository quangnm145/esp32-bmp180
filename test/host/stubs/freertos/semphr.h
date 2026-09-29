#pragma once
/* Host stub of FreeRTOS semphr.h: mutex operations are no-ops (tests are single-threaded). */
#include "freertos/FreeRTOS.h"
typedef void *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    static int dummy;
    return (SemaphoreHandle_t)&dummy;
}
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t h, TickType_t t) { (void)h; (void)t; return pdTRUE; }
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t h) { (void)h; return pdTRUE; }
