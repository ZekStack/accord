#pragma once

#include <cstdint>

extern uint32_t accordTestMillis;

inline uint32_t millis() {
	return accordTestMillis;
}
