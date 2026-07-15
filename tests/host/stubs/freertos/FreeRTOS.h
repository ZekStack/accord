#pragma once

#include <thread>

using TickType_t = unsigned int;

constexpr TickType_t portMAX_DELAY = 0xffffffffu;
constexpr int pdTRUE = 1;
constexpr int pdFALSE = 0;

inline void taskYIELD() {
	std::this_thread::yield();
}
