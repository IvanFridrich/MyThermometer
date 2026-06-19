#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "Config.h"
#include "anomaly.h"
#include "result.h"
#include "types.h"

// Phase 2 domain test: AnomalyDetector — classifies a Result<Temperature> into
// EventFlags.  Covers the consecutive-error threshold (FR-04), recovery, the
// plausibility window (-30..80 C, FR-03), the disconnect sentinel and the
// kTempInvalid bus-glitch path.
//
// Plausibility bounds (Config.h): min = -3000 (-30 C), max = 8000 (80 C),
// sentinel = 8500 (85 C).  Default threshold = 3 consecutive errors.

namespace {
Result<Temperature> ok(Temperature t) {
    return Result<Temperature>::ok(t);
}
Result<Temperature> err(Status s) {
    return Result<Temperature>::err(s);
}
} // namespace

// ---------------------------------------------------------------------------
// Healthy readings
// ---------------------------------------------------------------------------
TEST_CASE("a normal reading produces no flags") {
    AnomalyDetector det(3);
    CHECK(det.classify(ok(2300)) == 0U);
    CHECK(det.consecutiveErrors() == 0U);
}

TEST_CASE("plausibility bounds are inclusive") {
    AnomalyDetector det(3);
    CHECK(det.classify(ok(8000)) == 0U);  // exactly +80.00 C is valid
    CHECK(det.classify(ok(-3000)) == 0U); // exactly -30.00 C is valid
}

TEST_CASE("values just outside the window are weird") {
    AnomalyDetector det(3);
    CHECK((det.classify(ok(8001)) & cfg::flag::kWeirdValue) != 0U);  // +80.01 C
    CHECK((det.classify(ok(-3001)) & cfg::flag::kWeirdValue) != 0U); // -30.01 C
}

TEST_CASE("a weird value does not raise a sensor error or touch the counter") {
    AnomalyDetector det(3);
    EventFlags      f = det.classify(ok(9000)); // 90 C, out of range
    CHECK((f & cfg::flag::kWeirdValue) != 0U);
    CHECK((f & cfg::flag::kSensorOpen) == 0U);
    CHECK(det.consecutiveErrors() == 0U); // OK result path resets the counter
}

TEST_CASE("the disconnect sentinel reads as a weird value") {
    AnomalyDetector det(3);
    EventFlags      f = det.classify(ok(8500)); // 85.00 C sentinel
    CHECK((f & cfg::flag::kWeirdValue) != 0U);
}

TEST_CASE("kTempInvalid coming through the OK path is weird") {
    AnomalyDetector det(3);
    EventFlags      f = det.classify(ok(kTempInvalid));
    CHECK((f & cfg::flag::kWeirdValue) != 0U);
    CHECK(det.consecutiveErrors() == 0U);
}

TEST_CASE("negative temperatures inside the window are valid") {
    AnomalyDetector det(3);
    CHECK(det.classify(ok(-1234)) == 0U); // -12.34 C
}

// ---------------------------------------------------------------------------
// Error classification and the consecutive-error threshold
// ---------------------------------------------------------------------------
TEST_CASE("a single OneWire error sets its flag but not SENSOR_OPEN yet") {
    AnomalyDetector det(3);
    EventFlags      f = det.classify(err(Status::kOneWireErr));
    CHECK((f & cfg::flag::kOneWireErr) != 0U);
    CHECK((f & cfg::flag::kSensorOpen) == 0U);
    CHECK(det.consecutiveErrors() == 1U);
}

TEST_CASE("a sensor-open error sets SENSOR_OPEN immediately") {
    AnomalyDetector det(3);
    EventFlags      f = det.classify(err(Status::kSensorOpen));
    CHECK((f & cfg::flag::kSensorOpen) != 0U);
}

TEST_CASE("SENSOR_OPEN is raised once the error count reaches the threshold") {
    AnomalyDetector det(3);
    det.classify(err(Status::kOneWireErr));                // 1
    det.classify(err(Status::kOneWireErr));                // 2
    EventFlags f = det.classify(err(Status::kOneWireErr)); // 3 -> threshold
    CHECK((f & cfg::flag::kSensorOpen) != 0U);
    CHECK((f & cfg::flag::kOneWireErr) != 0U); // status flag still present
    CHECK(det.consecutiveErrors() == 3U);
}

TEST_CASE("a threshold of one raises SENSOR_OPEN on the first error") {
    AnomalyDetector det(1);
    EventFlags      f = det.classify(err(Status::kOneWireErr));
    CHECK((f & cfg::flag::kSensorOpen) != 0U);
}

TEST_CASE("an unmapped error status still counts toward the threshold") {
    AnomalyDetector det(2);
    EventFlags      f1 = det.classify(err(Status::kTimeout));
    CHECK((f1 & cfg::flag::kSensorOpen) == 0U);          // no status-specific flag, below threshold
    EventFlags f2 = det.classify(err(Status::kTimeout)); // reaches threshold 2
    CHECK((f2 & cfg::flag::kSensorOpen) != 0U);
}

// ---------------------------------------------------------------------------
// Recovery
// ---------------------------------------------------------------------------
TEST_CASE("a good reading resets the consecutive-error counter") {
    AnomalyDetector det(3);
    det.classify(err(Status::kOneWireErr));
    det.classify(err(Status::kOneWireErr));
    CHECK(det.consecutiveErrors() == 2U);
    det.classify(ok(2200)); // recovery
    CHECK(det.consecutiveErrors() == 0U);
    // A single fresh error must NOT immediately re-trip the threshold.
    EventFlags f = det.classify(err(Status::kOneWireErr));
    CHECK((f & cfg::flag::kSensorOpen) == 0U);
    CHECK(det.consecutiveErrors() == 1U);
}

TEST_CASE("explicit reset clears the counter") {
    AnomalyDetector det(3);
    det.classify(err(Status::kOneWireErr));
    det.classify(err(Status::kOneWireErr));
    det.reset();
    CHECK(det.consecutiveErrors() == 0U);
}

TEST_CASE("the default threshold is three consecutive errors") {
    AnomalyDetector det; // default ctor -> threshold 3 per FR-04
    CHECK((det.classify(err(Status::kOneWireErr)) & cfg::flag::kSensorOpen) == 0U);
    CHECK((det.classify(err(Status::kOneWireErr)) & cfg::flag::kSensorOpen) == 0U);
    CHECK((det.classify(err(Status::kOneWireErr)) & cfg::flag::kSensorOpen) != 0U);
}

TEST_CASE("SENSOR_OPEN stays set across a counter saturation boundary") {
    // Regression: an unsaturated uint8_t counter would wrap to 0 at the 256th
    // consecutive error and momentarily drop SENSOR_OPEN.  Pump well past 255.
    AnomalyDetector det(3);
    for (int i = 0; i < 600; ++i) {
        EventFlags f = det.classify(err(Status::kOneWireErr));
        if (i >= 2) { // threshold reached on the 3rd error and must never drop
            CHECK((f & cfg::flag::kSensorOpen) != 0U);
        }
    }
    CHECK(det.consecutiveErrors() >= 3U); // saturated, never wrapped to 0
}
