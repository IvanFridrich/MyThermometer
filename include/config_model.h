#pragma once
#include <cstdint>

// Persistent configuration — all fields map 1-to-1 to NVS keys (§6.4).
// Zero-initialised by default: always populate via defaults() or NVS load before use.
// Canonical default values live in Config.h (cfg::temp::*, cfg::ledc::*) — single source of truth.
struct ConfigModel {
    bool    beeperEnabled{};
    int16_t diffThresholdC100{};
    int16_t diffHysteresisC100{};
    int16_t fireThrC100{};
    int16_t fireHysteresisC100{};
    uint8_t lcdContrastPwm{};
    bool    emailEnabled{};
    uint8_t windowGoal{};
    int16_t quietFromMin{}; // window-advice quiet hours, minutes-of-day (0..1439)
    int16_t quietToMin{};

    static ConfigModel defaults();
    bool               validate() const;
};
