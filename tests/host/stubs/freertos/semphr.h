#pragma once

#include "FreeRTOS.h"

using SemaphoreHandle_t = void *;

inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() {
	return reinterpret_cast<void *>(1);
}

inline void vSemaphoreDelete(SemaphoreHandle_t) {
}

inline int xSemaphoreTakeRecursive(SemaphoreHandle_t, TickType_t) {
	return pdTRUE;
}

inline void xSemaphoreGiveRecursive(SemaphoreHandle_t) {
}
