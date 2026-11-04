#pragma once
#include <cstdint>
#include <cmath>
#include <cstring>
#define IRAM_ATTR
#define ESP_INTR_FLAG_IRAM 1
#define ESP_OK 0
#define ESP_FAIL -1
using esp_err_t=int;
extern uint32_t fakeMillis;
inline uint32_t millis(){return fakeMillis;}
inline uint32_t micros(){return fakeMillis*1000;}
