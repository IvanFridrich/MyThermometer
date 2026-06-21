#include "email_policy.h"

#include <cstddef>
#include <cstdint>

namespace email_policy {

EmailPolicy::EmailPolicy(uint32_t minIntervalMs) : minIntervalMs_(minIntervalMs) {}

bool EmailPolicy::shouldSend(Type type, bool active, bool enabled, uint32_t nowMs) {
    const size_t i          = idx(type);
    const bool   risingEdge = active && !prevActive_[i];
    prevActive_[i]          = active; // track state every cycle for edge detection

    if (!risingEdge || !enabled) {
        return false;
    }
    // First-ever send always passes; afterwards enforce the per-type interval
    // since the last *successful* send. Subtraction is modular (millis() wrap-safe).
    if (everSucceeded_[i] && (nowMs - lastSuccessMs_[i]) < minIntervalMs_) {
        return false;
    }
    return true;
}

void EmailPolicy::markSent(Type type, uint32_t nowMs) {
    const size_t i    = idx(type);
    lastSuccessMs_[i] = nowMs;
    everSucceeded_[i] = true;
}

void EmailPolicy::reset() {
    prevActive_.fill(false);
    everSucceeded_.fill(false);
    lastSuccessMs_.fill(0);
}

} // namespace email_policy
