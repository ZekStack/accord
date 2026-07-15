#pragma once

#include <atomic>
#include <cstdint>

extern std::atomic<uint32_t> accordTestMillis;

inline uint32_t millis() {
	return accordTestMillis.load(std::memory_order_relaxed);
}
