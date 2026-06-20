#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

#include "Config.h"

// Email alarm policy (SPECIFICATION.md §7). Pure decision logic — it does NOT
// send anything; the mail task performs the SMTP send and logs the outcome.
//
// Rules (per alarm type, FIRE and SENSOR_FAULT):
//   - An automatic email is sent ONLY on the rising edge of the alarm AND when
//     at least kMinIntervalMs has elapsed since the last automatic send.
//   - Persistence never re-sends: while an alarm stays active there is no new
//     edge, so no further automatic email — even after the interval elapses.
//   - Clearing then re-arming is a new edge -> eligible again (still <= 1/interval).
//   - When email is disabled, nothing is sent.
//   - A manual status email (web) bypasses this entirely; the caller just checks
//     that email is enabled, so it is not modelled here.
namespace email_policy {

enum class Type : uint8_t { kFire = 0, kSensorFault = 1 };
constexpr size_t kTypeCount = 2;
static_assert(static_cast<size_t>(Type::kSensorFault) + 1U == kTypeCount,
              "kTypeCount must cover every Type (per-type state arrays are sized by it)");

class EmailPolicy {
  public:
    explicit EmailPolicy(uint32_t minIntervalMs = cfg::email::kMinIntervalMs);

    // Call once per cycle per type with the current alarm state. Returns true iff
    // an automatic email should be sent now (rising edge + enabled + first-ever or
    // interval elapsed). On true it records nowMs as the rate-limit anchor.
    bool onAlarm(Type type, bool active, bool enabled, uint32_t nowMs);

    void reset();

  private:
    static size_t idx(Type type) { return static_cast<size_t>(type); }

    uint32_t                         minIntervalMs_;
    std::array<bool, kTypeCount>     prevActive_{};
    std::array<bool, kTypeCount>     everSent_{};
    std::array<uint32_t, kTypeCount> lastSentMs_{};
};

} // namespace email_policy
