#pragma once

#include "FreeRTOS.h"

#include <memory>
#include <mutex>

struct StaticSemaphore_t {
	std::recursive_mutex mutex;
};

using SemaphoreHandle_t = StaticSemaphore_t *;

inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic(StaticSemaphore_t *storage) {
	if (storage == nullptr) {
		return nullptr;
	}
	return std::construct_at(storage);
}

inline void vSemaphoreDelete(SemaphoreHandle_t handle) {
	if (handle != nullptr) {
		std::destroy_at(handle);
	}
}

inline int xSemaphoreTakeRecursive(SemaphoreHandle_t handle, TickType_t) {
	if (handle == nullptr) {
		return pdFALSE;
	}
	handle->mutex.lock();
	return pdTRUE;
}

inline void xSemaphoreGiveRecursive(SemaphoreHandle_t handle) {
	if (handle != nullptr) {
		handle->mutex.unlock();
	}
}
