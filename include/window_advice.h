#pragma once
#include <cstdint>

#include "types.h"

// Window recommendation — stateful, two OR-ed rules (D9, FR-07 rev.):
//
//   diff rule: open when inside is warmer than outside by >= diffThresholdC100;
//              back to closed once the temps equalize (inner - outer <= 0).
//              The whole N..0 band holds the previous state, so it doubles as
//              a maximal hysteresis — no flapping, no repeated melodies.
//   vent rule: open whenever the outdoor average is <= cfg::vent::kVentOpenC100
//              (20.00 C incl.), regardless of the indoor temperature
//              ("ventilation is effective now"); clears above kVentCloseC100
//              (20.50 C) so drift around the trip point cannot retrigger.
//
// An invalid average disables the rule that needs it. Runs on the 10-min
// averages (already smoothed). Shown on the display (window icon), in the
// JSON API, and drives the open/close melodies on the state flip.
namespace window {

enum class Advice : uint8_t {
    kClose = 0, // keep the window shut (base state)
    kOpen  = 1, // opening the window helps right now
};

class Advisor {
  public:
    // Evaluate both rules against the 10-min averages; returns the combined
    // recommendation. diffThresholdC100 = web-configurable N (centi-degC).
    Advice update(Temperature innerAvg, Temperature outerAvg, int16_t diffThresholdC100);

    [[nodiscard]] Advice current() const {
        return (diffOpen_ || ventOpen_) ? Advice::kOpen : Advice::kClose;
    }
    [[nodiscard]] bool diffActive() const { return diffOpen_; } // drives DIFF_EXCEEDED flag
    [[nodiscard]] bool ventActive() const { return ventOpen_; }

    void reset();

  private:
    bool diffOpen_{false};
    bool ventOpen_{false};
};

} // namespace window
