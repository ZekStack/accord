#pragma once

#include "FreeRTOS.h"
#include <new>

#include <mutex>

using SemaphoreHandle_t = std::recursive_mutex *;

inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() {
	return new (std::nothrow) std::recursive_mutex();
}

inline void vSemaphoreDelete(SemaphoreHandle_t handle) {
	delete handle;
}

inline int xSemaphoreTakeRecursive(SemaphoreHandle_t handle, TickType_t) {
	if (handle == nullptr) {
		return pdFALSE;
	}
	handle->lock();
	return pdTRUE;
}

inline void xSemaphoreGiveRecursive(SemaphoreHandle_t handle) {
	if (handle != nullptr) {
		handle->unlock();
	}
}
