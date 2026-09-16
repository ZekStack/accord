#pragma once

#include <strata/freertos/Mutex.h>

using AccordMutex = Strata::FreeRTOS::RecursiveMutex;

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
