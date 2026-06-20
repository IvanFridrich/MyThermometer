#include "window_advice.h"

#include <cstdint>

#include "Config.h"
#include "types.h"

namespace window {

Advice advise(Temperature innerAvg, Temperature outerAvg, cfg::window_advisor::Goal goal,
              int16_t diffThresholdC100) {
    if (innerAvg == kTempInvalid || outerAvg == kTempInvalid) {
        return Advice::kNoChange;
    }
    // diff > 0 => outside warmer than inside; diff < 0 => outside cooler.
    const int32_t diff    = static_cast<int32_t>(outerAvg) - static_cast<int32_t>(innerAvg);
    const int32_t absDiff = (diff < 0) ? -diff : diff;
    if (absDiff < diffThresholdC100) {
        return Advice::kNoChange;
    }

    const bool outerCooler = (diff < 0);
    if (goal == cfg::window_advisor::Goal::kCoolRoom) {
        // Want to cool: open when it is cooler outside, shut to keep heat out.
        return outerCooler ? Advice::kOpen : Advice::kClose;
    }
    // WarmRoom — want to warm: open when it is warmer outside, shut to keep cold out.
    return outerCooler ? Advice::kClose : Advice::kOpen;
}

} // namespace window
