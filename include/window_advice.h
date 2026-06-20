#pragma once
#include <cstdint>

#include "Config.h"
#include "types.h"

// Window recommendation — the user-facing purpose of the difference metric (D9).
// Pure function of the 10-min averages, the goal and the configured threshold;
// shown on the LCD status page (FR-11) and in the JSON API. Stateless: the
// 10-min averaging already smooths the inputs, so no extra hysteresis is kept.
namespace window {

enum class Advice : uint8_t {
    kNoChange = 0, // within threshold, or a reading is invalid
    kOpen     = 1, // opening the window moves the room toward the goal
    kClose    = 2, // the window should stay shut to hold the goal
};

// goal: cfg::window_advisor::Goal (CoolRoom = cool the room, WarmRoom = warm it).
// diffThresholdC100: magnitude (centi-°C) of |outerAvg - innerAvg| that triggers
// a recommendation (typically ConfigModel::diffThresholdC100).
[[nodiscard]] Advice advise(Temperature innerAvg, Temperature outerAvg,
                            cfg::window_advisor::Goal goal, int16_t diffThresholdC100);

} // namespace window
