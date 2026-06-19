#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "Config.h"
#include "alarm_state.h"
#include "config_model.h"
#include "types.h"

// Phase 2 domain test: AlarmState — fire / difference / sensor-fault state
// machine with hysteresis and rising/falling edge detection.
//
// Default thresholds (from ConfigModel::defaults / Config.h):
//   fire:  set >= 4500, clear < 4300  (45.0 / 43.0 C)
//   diff:  set |d| >= 200, clear |d| < 150  (2.0 / 1.5 C), d = outerAvg - innerAvg
//   CoolRoom goal (default): fires when outside is COOLER (d < 0).
//
// MeasurementSnapshot field order: {innerRaw, innerAvg, outerAvg, anomalyFlags}.

namespace {
// A snapshot that triggers no alarm: moderate inner, equal averages, no flags.
MeasurementSnapshot calm() {
    return MeasurementSnapshot{2300, 2300, 2300, 0};
}
} // namespace

// ---------------------------------------------------------------------------
// Fire alarm (instantaneous inner)
// ---------------------------------------------------------------------------
TEST_CASE("fire rises at the threshold") {
    ConfigModel cfg = ConfigModel::defaults();
    AlarmState  alm(cfg);
    AlarmEdges  e = alm.update(MeasurementSnapshot{4500, 2300, 2300, 0});
    CHECK(e.fireRising);
    CHECK_FALSE(e.fireFalling);
    CHECK(alm.isFire());
}

TEST_CASE("fire does not rise just below the threshold") {
    ConfigModel cfg = ConfigModel::defaults();
    AlarmState  alm(cfg);
    AlarmEdges  e = alm.update(MeasurementSnapshot{4499, 2300, 2300, 0});
    CHECK_FALSE(e.fireRising);
    CHECK_FALSE(alm.isFire());
}

TEST_CASE("fire does not flap inside the hysteresis band") {
    ConfigModel cfg = ConfigModel::defaults();
    AlarmState  alm(cfg);
    CHECK(alm.update(MeasurementSnapshot{4500, 2300, 2300, 0}).fireRising);
    // Drop into the band [4300, 4500): alarm must stay set, no edges.
    AlarmEdges hold1 = alm.update(MeasurementSnapshot{4400, 2300, 2300, 0});
    CHECK_FALSE(hold1.fireRising);
    CHECK_FALSE(hold1.fireFalling);
    CHECK(alm.isFire());
    // Exactly at the clear threshold (4300) still holds (clear is strict <).
    AlarmEdges hold2 = alm.update(MeasurementSnapshot{4300, 2300, 2300, 0});
    CHECK_FALSE(hold2.fireFalling);
    CHECK(alm.isFire());
}

TEST_CASE("fire clears below the clear threshold") {
    ConfigModel cfg = ConfigModel::defaults();
    AlarmState  alm(cfg);
    alm.update(MeasurementSnapshot{4600, 2300, 2300, 0});
    CHECK(alm.isFire());
    AlarmEdges e = alm.update(MeasurementSnapshot{4299, 2300, 2300, 0});
    CHECK(e.fireFalling);
    CHECK_FALSE(e.fireRising);
    CHECK_FALSE(alm.isFire());
}

TEST_CASE("fire emits only one rising edge while held") {
    ConfigModel cfg = ConfigModel::defaults();
    AlarmState  alm(cfg);
    CHECK(alm.update(MeasurementSnapshot{4700, 2300, 2300, 0}).fireRising);
    AlarmEdges again = alm.update(MeasurementSnapshot{4800, 2300, 2300, 0});
    CHECK_FALSE(again.fireRising); // already set; no repeated edge
    CHECK(alm.isFire());
}

TEST_CASE("invalid inner reading never triggers fire") {
    ConfigModel cfg = ConfigModel::defaults();
    AlarmState  alm(cfg);
    AlarmEdges  e = alm.update(MeasurementSnapshot{kTempInvalid, 2300, 2300, 0});
    CHECK_FALSE(e.fireRising);
    CHECK_FALSE(alm.isFire());
}

TEST_CASE("invalid inner does not clear an already-active fire") {
    ConfigModel cfg = ConfigModel::defaults();
    AlarmState  alm(cfg);
    alm.update(MeasurementSnapshot{4600, 2300, 2300, 0});
    CHECK(alm.isFire());
    AlarmEdges e = alm.update(MeasurementSnapshot{kTempInvalid, 2300, 2300, 0});
    CHECK_FALSE(e.fireFalling); // invalid reading is ignored, state preserved
    CHECK(alm.isFire());
}

// ---------------------------------------------------------------------------
// Difference alarm — CoolRoom (default): fires when outside is cooler
// ---------------------------------------------------------------------------
TEST_CASE("CoolRoom diff rises when outside is cooler by >= threshold") {
    ConfigModel cfg = ConfigModel::defaults();
    AlarmState  alm(cfg);
    // d = outerAvg - innerAvg = 2000 - 2300 = -300; |d| = 300 >= 200.
    AlarmEdges e = alm.update(MeasurementSnapshot{2300, 2300, 2000, 0});
    CHECK(e.diffRising);
    CHECK(alm.isDiff());
}

TEST_CASE("CoolRoom diff ignores the wrong direction") {
    ConfigModel cfg = ConfigModel::defaults();
    AlarmState  alm(cfg);
    // Outside WARMER: d = 2600 - 2300 = +300. CoolRoom wants d < 0 -> no alarm.
    AlarmEdges e = alm.update(MeasurementSnapshot{2300, 2300, 2600, 0});
    CHECK_FALSE(e.diffRising);
    CHECK_FALSE(alm.isDiff());
}

TEST_CASE("diff rises at exactly the threshold and not just below") {
    ConfigModel cfg = ConfigModel::defaults();
    {
        AlarmState alm(cfg);
        // |d| = 200 exactly -> rises.
        CHECK(alm.update(MeasurementSnapshot{2300, 2300, 2100, 0}).diffRising);
    }
    {
        AlarmState alm(cfg);
        // |d| = 199 -> does not rise.
        CHECK_FALSE(alm.update(MeasurementSnapshot{2300, 2300, 2101, 0}).diffRising);
    }
}

TEST_CASE("diff does not flap inside the hysteresis band") {
    ConfigModel cfg = ConfigModel::defaults();
    AlarmState  alm(cfg);
    CHECK(alm.update(MeasurementSnapshot{2300, 2300, 2000, 0}).diffRising); // |d|=300
    // |d| = 160 is inside [150, 200): stays set, no edge.
    AlarmEdges hold = alm.update(MeasurementSnapshot{2300, 2300, 2140, 0});
    CHECK_FALSE(hold.diffRising);
    CHECK_FALSE(hold.diffFalling);
    CHECK(alm.isDiff());
}

TEST_CASE("diff clears below the clear threshold") {
    ConfigModel cfg = ConfigModel::defaults();
    AlarmState  alm(cfg);
    alm.update(MeasurementSnapshot{2300, 2300, 2000, 0}); // set, |d|=300
    // |d| = 140 < 150 -> clears.
    AlarmEdges e = alm.update(MeasurementSnapshot{2300, 2300, 2160, 0});
    CHECK(e.diffFalling);
    CHECK_FALSE(alm.isDiff());
}

TEST_CASE("diff needs both averages valid") {
    ConfigModel cfg = ConfigModel::defaults();
    {
        AlarmState alm(cfg);
        CHECK_FALSE(alm.update(MeasurementSnapshot{2300, kTempInvalid, 2000, 0}).diffRising);
        CHECK_FALSE(alm.isDiff());
    }
    {
        AlarmState alm(cfg);
        CHECK_FALSE(alm.update(MeasurementSnapshot{2300, 2300, kTempInvalid, 0}).diffRising);
        CHECK_FALSE(alm.isDiff());
    }
}

// ---------------------------------------------------------------------------
// Difference alarm — WarmRoom goal: fires when outside is warmer
// ---------------------------------------------------------------------------
TEST_CASE("WarmRoom diff rises when outside is warmer by >= threshold") {
    ConfigModel cfg = ConfigModel::defaults();
    cfg.windowGoal  = static_cast<uint8_t>(cfg::window_advisor::Goal::kWarmRoom);
    AlarmState alm(cfg);
    // d = 2600 - 2300 = +300; WarmRoom wants d > 0 -> alarm.
    AlarmEdges e = alm.update(MeasurementSnapshot{2300, 2300, 2600, 0});
    CHECK(e.diffRising);
    CHECK(alm.isDiff());
}

TEST_CASE("WarmRoom diff ignores the wrong direction") {
    ConfigModel cfg = ConfigModel::defaults();
    cfg.windowGoal  = static_cast<uint8_t>(cfg::window_advisor::Goal::kWarmRoom);
    AlarmState alm(cfg);
    // Outside cooler: d = -300. WarmRoom wants d > 0 -> no alarm.
    AlarmEdges e = alm.update(MeasurementSnapshot{2300, 2300, 2000, 0});
    CHECK_FALSE(e.diffRising);
    CHECK_FALSE(alm.isDiff());
}

// ---------------------------------------------------------------------------
// Sensor-fault alarm (driven by anomaly flags)
// ---------------------------------------------------------------------------
TEST_CASE("sensor fault rises from SENSOR_OPEN flag") {
    ConfigModel cfg = ConfigModel::defaults();
    AlarmState  alm(cfg);
    AlarmEdges  e = alm.update(MeasurementSnapshot{2300, 2300, 2300, cfg::flag::kSensorOpen});
    CHECK(e.sensorRising);
    CHECK(alm.isSensorFault());
}

TEST_CASE("sensor fault rises from ONEWIRE_ERR flag") {
    ConfigModel cfg = ConfigModel::defaults();
    AlarmState  alm(cfg);
    AlarmEdges  e = alm.update(MeasurementSnapshot{2300, 2300, 2300, cfg::flag::kOneWireErr});
    CHECK(e.sensorRising);
    CHECK(alm.isSensorFault());
}

TEST_CASE("sensor fault clears when flags clear") {
    ConfigModel cfg = ConfigModel::defaults();
    AlarmState  alm(cfg);
    alm.update(MeasurementSnapshot{2300, 2300, 2300, cfg::flag::kSensorOpen});
    CHECK(alm.isSensorFault());
    AlarmEdges e = alm.update(calm());
    CHECK(e.sensorFalling);
    CHECK_FALSE(alm.isSensorFault());
}

TEST_CASE("sensor fault emits only one rising edge while held") {
    ConfigModel cfg = ConfigModel::defaults();
    AlarmState  alm(cfg);
    CHECK(alm.update(MeasurementSnapshot{2300, 2300, 2300, cfg::flag::kSensorOpen}).sensorRising);
    AlarmEdges again = alm.update(MeasurementSnapshot{2300, 2300, 2300, cfg::flag::kOneWireErr});
    CHECK_FALSE(again.sensorRising);
    CHECK(alm.isSensorFault());
}

// ---------------------------------------------------------------------------
// Combined / lifecycle
// ---------------------------------------------------------------------------
TEST_CASE("all three alarms can rise in a single update") {
    ConfigModel cfg = ConfigModel::defaults();
    AlarmState  alm(cfg);
    // inner hot (fire), outside cooler by 300 (diff CoolRoom), sensor flag set.
    AlarmEdges e = alm.update(MeasurementSnapshot{4600, 2300, 2000, cfg::flag::kSensorOpen});
    CHECK(e.fireRising);
    CHECK(e.diffRising);
    CHECK(e.sensorRising);
    CHECK(e.any());
}

TEST_CASE("a calm snapshot produces no edges") {
    ConfigModel cfg = ConfigModel::defaults();
    AlarmState  alm(cfg);
    AlarmEdges  e = alm.update(calm());
    CHECK_FALSE(e.any());
}

TEST_CASE("reset clears every latched alarm") {
    ConfigModel cfg = ConfigModel::defaults();
    AlarmState  alm(cfg);
    alm.update(MeasurementSnapshot{4600, 2300, 2000, cfg::flag::kSensorOpen});
    CHECK(alm.isFire());
    CHECK(alm.isDiff());
    CHECK(alm.isSensorFault());
    alm.reset();
    CHECK_FALSE(alm.isFire());
    CHECK_FALSE(alm.isDiff());
    CHECK_FALSE(alm.isSensorFault());
    // After reset, the same hot snapshot rises again (proves state was cleared).
    CHECK(alm.update(MeasurementSnapshot{4600, 2300, 2000, 0}).fireRising);
}
