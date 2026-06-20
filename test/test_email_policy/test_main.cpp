#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdint>

#include "Config.h"
#include "email_policy.h"

// Phase 5 domain test: email alarm policy (SPECIFICATION.md §7). Rising-edge
// only, max one auto email per interval per type, persistence never re-sends,
// clear+re-arm is a fresh edge, disabled suppresses, types are independent, and
// the interval comparison survives a millis() wrap.

namespace {
using email_policy::EmailPolicy;
using Type                 = email_policy::Type;
constexpr uint32_t kHr     = cfg::email::kMinIntervalMs; // 3 600 000 ms
constexpr Type     kFire   = Type::kFire;
constexpr Type     kSensor = Type::kSensorFault;
} // namespace

TEST_CASE("a rising edge sends exactly one automatic email") {
    EmailPolicy p;
    CHECK(p.onAlarm(kFire, true, true, 1000));
}

TEST_CASE("persistence never re-sends, even after the interval elapses") {
    EmailPolicy p;
    CHECK(p.onAlarm(kFire, true, true, 1000));
    CHECK_FALSE(p.onAlarm(kFire, true, true, 2000));           // still active, no new edge
    CHECK_FALSE(p.onAlarm(kFire, true, true, 1000 + 2 * kHr)); // 2 h later, still no edge
}

TEST_CASE("a falling edge sends nothing") {
    EmailPolicy p;
    CHECK(p.onAlarm(kFire, true, true, 1000));
    CHECK_FALSE(p.onAlarm(kFire, false, true, 2000));
}

TEST_CASE("clear then re-arm within the interval is rate-limited") {
    EmailPolicy p;
    CHECK(p.onAlarm(kFire, true, true, 1000)); // sent, anchor = 1000
    CHECK_FALSE(p.onAlarm(kFire, false, true, 2000));
    CHECK_FALSE(p.onAlarm(kFire, true, true, 1000 + kHr - 1)); // new edge but too soon
}

TEST_CASE("clear then re-arm after the interval sends again") {
    EmailPolicy p;
    CHECK(p.onAlarm(kFire, true, true, 1000));
    CHECK_FALSE(p.onAlarm(kFire, false, true, 2000));
    CHECK(p.onAlarm(kFire, true, true, 1000 + kHr)); // interval elapsed -> new edge sends
}

TEST_CASE("a disabled policy never sends automatically") {
    EmailPolicy p;
    CHECK_FALSE(p.onAlarm(kFire, true, false, 1000)); // rising edge but disabled
}

TEST_CASE("an edge that occurs while disabled is not replayed on re-enable") {
    EmailPolicy p;
    CHECK_FALSE(p.onAlarm(kFire, true, false, 1000)); // edge consumed while disabled
    CHECK_FALSE(p.onAlarm(kFire, true, true, 2000));  // enabled now, but no new edge
    CHECK_FALSE(p.onAlarm(kFire, false, true, 3000)); // clear
    CHECK(p.onAlarm(kFire, true, true, 4000));        // genuine new edge -> sends
}

TEST_CASE("fire and sensor-fault are tracked independently") {
    EmailPolicy p;
    CHECK(p.onAlarm(kFire, true, true, 1000));
    CHECK(p.onAlarm(kSensor, true, true, 1000)); // own first edge, independent
    CHECK_FALSE(p.onAlarm(kFire, true, true, 2000));
    CHECK_FALSE(p.onAlarm(kSensor, true, true, 2000));
}

TEST_CASE("a suppressed re-arm does not advance the rate-limit anchor") {
    EmailPolicy p;
    CHECK(p.onAlarm(kFire, true, true, 1000)); // sent, anchor = 1000
    CHECK_FALSE(p.onAlarm(kFire, false, true, 2000));
    CHECK_FALSE(p.onAlarm(kFire, true, true, 1000 + kHr - 1)); // re-arm too soon, suppressed
    CHECK_FALSE(p.onAlarm(kFire, false, true, 1000 + kHr));
    // The anchor is still 1000 (not the suppressed attempt), so a re-arm just
    // past one interval from the original send is eligible again.
    CHECK(p.onAlarm(kFire, true, true, 1000 + kHr + 1));
}

TEST_CASE("the first-ever edge sends even at a tiny uptime") {
    EmailPolicy p;
    CHECK(p.onAlarm(kFire, true, true, 5)); // 5 < interval, but never sent before
}

TEST_CASE("reset clears edge and rate-limit state") {
    EmailPolicy p;
    CHECK(p.onAlarm(kFire, true, true, 1000));
    p.reset();
    // After reset the alarm is "inactive" again, so a fresh active reading is a
    // new first-ever edge and sends immediately.
    CHECK(p.onAlarm(kFire, true, true, 2000));
}

TEST_CASE("the interval comparison survives a millis() wrap") {
    SUBCASE("too soon across the wrap is suppressed") {
        EmailPolicy    q;
        const uint32_t w0 = 0xFFFFFF00U;
        CHECK(q.onAlarm(kFire, true, true, w0));
        CHECK_FALSE(q.onAlarm(kFire, false, true, w0 + 10U));
        CHECK_FALSE(q.onAlarm(kFire, true, true, w0 + 1000U)); // wraps; only ~1 s elapsed
    }
    SUBCASE("a full interval across the wrap sends") {
        EmailPolicy    q;
        const uint32_t t0 = 0xFFFF0000U;
        CHECK(q.onAlarm(kFire, true, true, t0));
        CHECK_FALSE(q.onAlarm(kFire, false, true, t0 + 10U));
        CHECK(q.onAlarm(kFire, true, true, t0 + kHr)); // modular diff == kHr
    }
}
