#pragma once
#include "platform.h"
#ifndef portMAX_DELAY
#define portMAX_DELAY UINT32_MAX
#endif
typedef struct test_semaphore *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t semaphore, uint32_t ticks);
int xSemaphoreGive(SemaphoreHandle_t semaphore);
