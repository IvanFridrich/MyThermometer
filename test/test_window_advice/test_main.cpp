#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "Config.h"
#include "types.h"
#include "window_advice.h"

// Stateful window advisor (FR-07 rev.): two OR-ed rules.
//   diff rule: open at inner - outer >= N, close at inner - outer <= 0,
//              hold anywhere between (the N..0 band is the hysteresis).
//   vent rule: open at outerAvg <= 20.00 C (incl.), close above 20.50 C,
//              hold in the 20.00..20.50 band; independent of the indoor temp.
// Invalid averages disable the rule that needs them.

namespace {
using window::Advice;
using window::Advisor;
constexpr int16_t kN = 200; // threshold N = 2.00 C

// Outer value safely above the vent band so only the diff rule is in play.
constexpr Temperature kWarmOuter = 2400; // 24.00 C
} // namespace

TEST_CASE("initial state is closed") {
    Advisor a;
    CHECK(a.current() == Advice::kClose);
    CHECK_FALSE(a.diffActive());
    CHECK_FALSE(a.ventActive());
}

// ---------------------------------------------------------------------------
// diff rule
// ---------------------------------------------------------------------------
TEST_CASE("diff rule opens at inner - outer >= N and closes once equalized") {
    Advisor a;
    // inside 26.4, outside 24.0 -> diff 2.4 >= 2.0 -> open
    CHECK(a.update(2640, kWarmOuter, kN) == Advice::kOpen);
    CHECK(a.diffActive());
    // room cools: diff 1.0 -> inside the N..0 band -> still open (hold)
    CHECK(a.update(2500, kWarmOuter, kN) == Advice::kOpen);
    // equalized: diff 0 -> closed
    CHECK(a.update(kWarmOuter, kWarmOuter, kN) == Advice::kClose);
    CHECK_FALSE(a.diffActive());
}

TEST_CASE("diff rule does not open below N") {
    Advisor a;
    CHECK(a.update(2590, kWarmOuter, kN) == Advice::kClose); // diff 1.90 < N
    CHECK(a.update(2599, kWarmOuter, kN) == Advice::kClose); // diff 1.99 < N
    CHECK(a.update(2600, kWarmOuter, kN) == Advice::kOpen);  // diff 2.00 == N
}

TEST_CASE("outside warmer than inside never opens via the diff rule") {
    Advisor a;
    // outside warmer by 3 C (old WarmRoom scenario) -> stays closed
    CHECK(a.update(2400, 2700, kN) == Advice::kClose);
}

TEST_CASE("no flapping when the diff oscillates inside the N..0 band") {
    Advisor a;
    CHECK(a.update(2640, kWarmOuter, kN) == Advice::kOpen);
    // noise between 0.1 and 1.9 C keeps the state open the whole time
    CHECK(a.update(2410, kWarmOuter, kN) == Advice::kOpen);
    CHECK(a.update(2590, kWarmOuter, kN) == Advice::kOpen);
    CHECK(a.update(2410, kWarmOuter, kN) == Advice::kOpen);
    // and only equalizing closes it
    CHECK(a.update(2395, kWarmOuter, kN) == Advice::kClose);
}

TEST_CASE("invalid averages disable the diff rule") {
    Advisor a;
    CHECK(a.update(2640, kWarmOuter, kN) == Advice::kOpen);
    CHECK(a.update(kTempInvalid, kWarmOuter, kN) == Advice::kClose); // inner lost
    CHECK(a.update(2640, kTempInvalid, kN) == Advice::kClose);       // outer lost kills both rules
}

// ---------------------------------------------------------------------------
// vent rule (fixed 20.00 C trip incl., 20.50 C clear)
// ---------------------------------------------------------------------------
TEST_CASE("vent rule opens at exactly 20.00 C outdoors, regardless of indoors") {
    Advisor a;
    // inside COLDER than outside — diff rule would never open here
    CHECK(a.update(1800, cfg::vent::kVentOpenC100, kN) == Advice::kOpen);
    CHECK(a.ventActive());
    CHECK_FALSE(a.diffActive());
}

TEST_CASE("vent rule holds inside the 20.00..20.50 band and clears above it") {
    Advisor a;
    // inside colder than outside throughout -> the diff rule stays out of play
    CHECK(a.update(1800, 1900, kN) == Advice::kOpen); // 19.0 C -> vent on
    // drift into the band: still open (no melody retrigger around 20.0)
    CHECK(a.update(1800, 2020, kN) == Advice::kOpen);
    CHECK(a.update(1800, cfg::vent::kVentCloseC100, kN) == Advice::kOpen); // 20.50 still holds
    // above the clear point -> off
    CHECK(a.update(1800, 2051, kN) == Advice::kClose);
    CHECK_FALSE(a.ventActive());
    // and it does not re-open until <= 20.00 again
    CHECK(a.update(1800, 2049, kN) == Advice::kClose); // 20.49: inside band, stays off
    CHECK(a.update(1800, 2000, kN) == Advice::kOpen);  // 20.00 incl. -> on
}

TEST_CASE("invalid outer average disables the vent rule") {
    Advisor a;
    CHECK(a.update(2500, 1500, kN) == Advice::kOpen);
    CHECK(a.update(2500, kTempInvalid, kN) == Advice::kClose);
}

// ---------------------------------------------------------------------------
// combination
// ---------------------------------------------------------------------------
TEST_CASE("advice stays open while either rule holds") {
    Advisor a;
    // both rules on: inside 26.4, outside 19.0
    CHECK(a.update(2640, 1900, kN) == Advice::kOpen);
    CHECK(a.diffActive());
    CHECK(a.ventActive());
    // outside warms to 24.0: vent off, diff still >= N -> open
    CHECK(a.update(2640, 2400, kN) == Advice::kOpen);
    CHECK_FALSE(a.ventActive());
    // equalize -> everything off -> closed
    CHECK(a.update(2400, 2400, kN) == Advice::kClose);
}

TEST_CASE("reset returns to closed") {
    Advisor a;
    CHECK(a.update(2640, 1900, kN) == Advice::kOpen);
    a.reset();
    CHECK(a.current() == Advice::kClose);
    CHECK_FALSE(a.diffActive());
    CHECK_FALSE(a.ventActive());
}
