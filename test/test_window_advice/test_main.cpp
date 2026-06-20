#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "Config.h"
#include "types.h"
#include "window_advice.h"

// Phase 4 domain test: window advisor. Truth table over both goals × both
// directions × threshold band, plus the invalid-reading and equal-temp cases.
// Threshold fixed at 200 centi-°C (2.0 °C, the default) for these cases.

namespace {
constexpr int16_t kThr = 200;
using Goal             = cfg::window_advisor::Goal;
using window::Advice;

Advice cool(Temperature inner, Temperature outer) {
    return window::advise(inner, outer, Goal::kCoolRoom, kThr);
}
Advice warm(Temperature inner, Temperature outer) {
    return window::advise(inner, outer, Goal::kWarmRoom, kThr);
}
} // namespace

// --- CoolRoom: open when outside is cooler, shut when outside is warmer ---
TEST_CASE("CoolRoom opens when outside is cooler by >= threshold") {
    CHECK(cool(2300, 2000) == Advice::kOpen); // outside 3 °C cooler
}
TEST_CASE("CoolRoom closes when outside is warmer by >= threshold") {
    CHECK(cool(2300, 2600) == Advice::kClose); // outside 3 °C warmer
}

// --- WarmRoom: open when outside is warmer, shut when outside is cooler ---
TEST_CASE("WarmRoom opens when outside is warmer by >= threshold") {
    CHECK(warm(2300, 2600) == Advice::kOpen);
}
TEST_CASE("WarmRoom closes when outside is cooler by >= threshold") {
    CHECK(warm(2300, 2000) == Advice::kClose);
}

// --- Threshold band ---
TEST_CASE("exactly at threshold triggers a recommendation") {
    CHECK(cool(2300, 2100) == Advice::kOpen);  // |diff| = 200 == threshold
    CHECK(cool(2300, 2500) == Advice::kClose); // |diff| = 200 == threshold
}
TEST_CASE("just below threshold is no change") {
    CHECK(cool(2300, 2101) == Advice::kNoChange); // |diff| = 199
    CHECK(warm(2300, 2499) == Advice::kNoChange); // |diff| = 199
}
TEST_CASE("equal temperatures are no change for both goals") {
    CHECK(cool(2300, 2300) == Advice::kNoChange);
    CHECK(warm(2300, 2300) == Advice::kNoChange);
}

// --- Invalid readings cannot produce a recommendation ---
TEST_CASE("an invalid reading yields no change") {
    CHECK(cool(kTempInvalid, 2000) == Advice::kNoChange);
    CHECK(cool(2300, kTempInvalid) == Advice::kNoChange);
    CHECK(warm(kTempInvalid, kTempInvalid) == Advice::kNoChange);
}

// --- Negative temperatures behave like any other values ---
TEST_CASE("negative temperatures are handled by sign, not magnitude") {
    CHECK(cool(-1000, -1500) == Advice::kOpen);  // outside 5 °C cooler -> cool: open
    CHECK(warm(-1000, -1500) == Advice::kClose); // outside cooler -> warm: close
}

// --- Degenerate threshold (rejected upstream by ConfigModel::validate) ---
TEST_CASE("a non-positive threshold cannot yield no-change from the band") {
    // diffThresholdC100 <= 0 is rejected by ConfigModel::validate, so it cannot
    // reach production; this only pins the behaviour: equal temps at threshold 0
    // skip the band check and resolve by direction (not outer-cooler -> CLOSE).
    CHECK(window::advise(2300, 2300, Goal::kCoolRoom, 0) == Advice::kClose);
}

// --- A custom (larger) threshold is honoured ---
TEST_CASE("a larger threshold suppresses a small difference") {
    CHECK(window::advise(2300, 2000, Goal::kCoolRoom, 400) == Advice::kNoChange); // |diff|=300<400
    CHECK(window::advise(2300, 1800, Goal::kCoolRoom, 400) == Advice::kOpen);     // |diff|=500>=400
}
