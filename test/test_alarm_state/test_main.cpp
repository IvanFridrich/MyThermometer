#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "Config.h"
#include "alarm_state.h"
#include "config_model.h"
#include "types.h"

// Phase 2 domain test: AlarmState — fire / sensor-fault state machine with
// hysteresis and rising/falling edge detection. (The temperature-difference
// state lives in window::Advisor — see test_window_advice.)
//
// Default thresholds (from ConfigModel::defaults / Config.h):
//   fire:  set >= 4500, clear < 4300  (45.0 / 43.0 C)
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
TEST_CASE("both alarms can rise in a single update") {
    ConfigModel cfg = ConfigModel::defaults();
    AlarmState  alm(cfg);
    // inner hot (fire) + sensor flag set.
    AlarmEdges e = alm.update(MeasurementSnapshot{4600, 2300, 2000, cfg::flag::kSensorOpen});
    CHECK(e.fireRising);
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
    CHECK(alm.isSensorFault());
    alm.reset();
    CHECK_FALSE(alm.isFire());
    CHECK_FALSE(alm.isSensorFault());
    // After reset, the same hot snapshot rises again (proves state was cleared).
    CHECK(alm.update(MeasurementSnapshot{4600, 2300, 2000, 0}).fireRising);
}
