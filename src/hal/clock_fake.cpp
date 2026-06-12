#include "clock.h"

#include <cstdint>

namespace {
uint32_t g_now_ms = 0;
} // namespace

// NOLINT(readability-convert-member-functions-to-static): ADR-09 — all Clock
// instances intentionally share one global counter in native builds.
uint32_t Clock::millis() { // NOLINT(readability-convert-member-functions-to-static)
    return g_now_ms;
}

void Clock::advanceMs(uint32_t delta) { // NOLINT(readability-convert-member-functions-to-static)
    g_now_ms += delta;
}

void Clock::reset() { // NOLINT(readability-convert-member-functions-to-static)
    g_now_ms = 0;
}
