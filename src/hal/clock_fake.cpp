#include "clock.h"

static uint32_t g_now_ms = 0;

uint32_t Clock::millis() {
    return g_now_ms;
}

void Clock::advanceMs(uint32_t delta) {
    g_now_ms += delta;
}

void Clock::reset() {
    g_now_ms = 0;
}
