#pragma once
using SemaphoreHandle_t=void*;
constexpr unsigned portMAX_DELAY=0xffffffffu;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return reinterpret_cast<void*>(1); }
inline void xSemaphoreTake(SemaphoreHandle_t,unsigned) {}
inline void xSemaphoreGive(SemaphoreHandle_t) {}
