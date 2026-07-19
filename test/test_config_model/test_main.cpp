#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "Config.h"
#include "config_model.h"

// Phase 2 domain test: ConfigModel — default construction (§6.4) and validate().
// Every rejection branch of validate() is exercised, plus the boundary cases
// that must remain valid.

// ---------------------------------------------------------------------------
// Defaults
// ---------------------------------------------------------------------------
TEST_CASE("defaults are valid") {
    ConfigModel m = ConfigModel::defaults();
    CHECK(m.validate());
}

TEST_CASE("defaults match the NVS contract (§6.4)") {
    ConfigModel m = ConfigModel::defaults();
    CHECK(m.beeperEnabled == true);
    CHECK(m.diffThresholdC100 == 200);
    CHECK(m.diffHysteresisC100 == 50);
    CHECK(m.fireThrC100 == 4500);
    CHECK(m.fireHysteresisC100 == 200);
    CHECK(m.lcdContrastPwm == cfg::ledc::kBacklightDefault);
    CHECK(m.emailEnabled == true);
    CHECK(m.windowGoal == static_cast<uint8_t>(cfg::window_advisor::Goal::kCoolRoom));
    CHECK(m.quietFromMin == 1320); // 22:00
    CHECK(m.quietToMin == 540);    // 09:00
}

// ---------------------------------------------------------------------------
// Difference-threshold validation
// ---------------------------------------------------------------------------
TEST_CASE("a non-positive diff threshold is rejected") {
    ConfigModel m       = ConfigModel::defaults();
    m.diffThresholdC100 = 0;
    CHECK_FALSE(m.validate());
    m.diffThresholdC100 = -1;
    CHECK_FALSE(m.validate());
}

TEST_CASE("a negative diff hysteresis is rejected") {
    ConfigModel m        = ConfigModel::defaults();
    m.diffHysteresisC100 = -1;
    CHECK_FALSE(m.validate());
}

TEST_CASE("diff hysteresis must be strictly below the diff threshold") {
    ConfigModel m        = ConfigModel::defaults();
    m.diffHysteresisC100 = m.diffThresholdC100; // equal -> invalid
    CHECK_FALSE(m.validate());
    m.diffHysteresisC100 = static_cast<int16_t>(m.diffThresholdC100 + 1); // greater -> invalid
    CHECK_FALSE(m.validate());
}

TEST_CASE("zero diff hysteresis is allowed") {
    ConfigModel m        = ConfigModel::defaults();
    m.diffHysteresisC100 = 0;
    CHECK(m.validate());
}

// ---------------------------------------------------------------------------
// Fire-threshold validation
// ---------------------------------------------------------------------------
TEST_CASE("a non-positive fire threshold is rejected") {
    ConfigModel m = ConfigModel::defaults();
    m.fireThrC100 = 0;
    CHECK_FALSE(m.validate());
    m.fireThrC100 = -100;
    CHECK_FALSE(m.validate());
}

TEST_CASE("a negative fire hysteresis is rejected") {
    ConfigModel m        = ConfigModel::defaults();
    m.fireHysteresisC100 = -1;
    CHECK_FALSE(m.validate());
}

TEST_CASE("fire hysteresis must be strictly below the fire threshold") {
    ConfigModel m        = ConfigModel::defaults();
    m.fireHysteresisC100 = m.fireThrC100; // equal -> invalid
    CHECK_FALSE(m.validate());
    m.fireHysteresisC100 = static_cast<int16_t>(m.fireThrC100 + 1); // greater -> invalid
    CHECK_FALSE(m.validate());
}

TEST_CASE("zero fire hysteresis is allowed") {
    ConfigModel m        = ConfigModel::defaults();
    m.fireHysteresisC100 = 0;
    CHECK(m.validate());
}

// ---------------------------------------------------------------------------
// Window goal validation
// ---------------------------------------------------------------------------
TEST_CASE("both window-goal values are accepted") {
    ConfigModel m = ConfigModel::defaults();
    m.windowGoal  = static_cast<uint8_t>(cfg::window_advisor::Goal::kCoolRoom); // 0
    CHECK(m.validate());
    m.windowGoal = static_cast<uint8_t>(cfg::window_advisor::Goal::kWarmRoom); // 1
    CHECK(m.validate());
}

TEST_CASE("an out-of-range window goal is rejected") {
    ConfigModel m = ConfigModel::defaults();
    m.windowGoal  = 2;
    CHECK_FALSE(m.validate());
    m.windowGoal = 255;
    CHECK_FALSE(m.validate());
}

// ---------------------------------------------------------------------------
// Quiet-hours validation (minutes-of-day, 0..1439)
// ---------------------------------------------------------------------------
TEST_CASE("quiet-hours boundaries 0 and 1439 are accepted") {
    ConfigModel m  = ConfigModel::defaults();
    m.quietFromMin = 0;
    m.quietToMin   = 1439;
    CHECK(m.validate());
}

TEST_CASE("equal quiet-hours (disabled) are accepted") {
    ConfigModel m  = ConfigModel::defaults();
    m.quietFromMin = 600;
    m.quietToMin   = 600;
    CHECK(m.validate());
}

TEST_CASE("out-of-range quiet-hours are rejected") {
    ConfigModel m  = ConfigModel::defaults();
    m.quietFromMin = 1440;
    CHECK_FALSE(m.validate());
    m            = ConfigModel::defaults();
    m.quietToMin = -1;
    CHECK_FALSE(m.validate());
}
