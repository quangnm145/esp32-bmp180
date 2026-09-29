#pragma once
/* Host stub of FreeRTOS.h for unit tests (single-threaded, no locking). */
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY ((TickType_t)0xffffffffu)
