#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class AccordMutex {
  public:
	AccordMutex() {
		_handle = xSemaphoreCreateRecursiveMutex();
	}

	~AccordMutex() {
		if (_handle != nullptr) {
			vSemaphoreDelete(_handle);
			_handle = nullptr;
		}
	}

	AccordMutex(const AccordMutex &) = delete;
	AccordMutex &operator=(const AccordMutex &) = delete;

	bool lock(TickType_t timeout = portMAX_DELAY) {
		return _handle != nullptr && xSemaphoreTakeRecursive(_handle, timeout) == pdTRUE;
	}

	void unlock() {
		if (_handle != nullptr) {
			xSemaphoreGiveRecursive(_handle);
		}
	}

  private:
	SemaphoreHandle_t _handle = nullptr;
};

class AccordLock {
  public:
	explicit AccordLock(AccordMutex &mutex) : _mutex(mutex), _locked(mutex.lock()) {
	}

	~AccordLock() {
		if (_locked) {
			_mutex.unlock();
		}
	}

	AccordLock(const AccordLock &) = delete;
	AccordLock &operator=(const AccordLock &) = delete;

	explicit operator bool() const {
		return _locked;
	}

  private:
	AccordMutex &_mutex;
	bool _locked = false;
};
