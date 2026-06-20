#include "email_policy.h"

#include <cstddef>
#include <cstdint>

namespace email_policy {

EmailPolicy::EmailPolicy(uint32_t minIntervalMs) : minIntervalMs_(minIntervalMs) {}

bool EmailPolicy::onAlarm(Type type, bool active, bool enabled, uint32_t nowMs) {
    const size_t i          = idx(type);
    const bool   risingEdge = active && !prevActive_[i];
    prevActive_[i]          = active; // track state every cycle for edge detection

    if (!risingEdge || !enabled) {
        return false;
    }
    // First-ever send always passes; afterwards enforce the per-type interval.
    // The subtraction is modular, so it stays correct across a millis() wrap.
    if (everSent_[i] && (nowMs - lastSentMs_[i]) < minIntervalMs_) {
        return false;
    }
    lastSentMs_[i] = nowMs;
    everSent_[i]   = true;
    return true;
}

void EmailPolicy::reset() {
    prevActive_.fill(false);
    everSent_.fill(false);
    lastSentMs_.fill(0);
}

} // namespace email_policy
