#include "window_advice.h"

#include <cstdint>

#include "Config.h"
#include "types.h"

namespace window {

Advice Advisor::update(Temperature innerAvg, Temperature outerAvg, int16_t diffThresholdC100) {
    // --- diff rule: needs both averages ------------------------------------
    if (innerAvg == kTempInvalid || outerAvg == kTempInvalid) {
        diffOpen_ = false;
    } else {
        // insideWarmer > 0 => inside warmer than outside (opening cools).
        const int32_t insideWarmer =
            static_cast<int32_t>(innerAvg) - static_cast<int32_t>(outerAvg);
        if (insideWarmer >= diffThresholdC100) {
            diffOpen_ = true; // opening cools the room by >= N
        } else if (insideWarmer <= 0) {
            diffOpen_ = false; // equalized — no benefit left
        }
        // 0 < insideWarmer < N: hold previous state (built-in hysteresis band)
    }

    // --- vent rule: outdoor-only, fixed 20.0 C trip / 20.5 C clear ---------
    if (outerAvg == kTempInvalid) {
        ventOpen_ = false;
    } else if (outerAvg <= cfg::vent::kVentOpenC100) {
        ventOpen_ = true; // <= 20.00 C incl. — always worth ventilating
    } else if (outerAvg > cfg::vent::kVentCloseC100) {
        ventOpen_ = false; // above 20.50 C — rule off
    }
    // 20.00 < outer <= 20.50: hold previous state (anti-flap around the trip)

    return current();
}

void Advisor::reset() {
    diffOpen_ = false;
    ventOpen_ = false;
}

} // namespace window
