#include "alarm_state.h"

#include "Config.h"

// Phase 2 implements full hysteresis logic.

AlarmState::AlarmState(const ConfigModel& cfg) : cfg_(cfg) {}

AlarmEdges AlarmState::update(const MeasurementSnapshot& snap) {
    AlarmEdges edges{};

    // --- Fire (instantaneous inner, FR-08) ---
    const bool innerValid = (snap.innerRaw != kTempInvalid);
    if (innerValid) {
        bool newFire = false;
        if (!fire_) {
            newFire = (snap.innerRaw >= cfg_.fireThrC100);
        } else {
            newFire = (snap.innerRaw >= (cfg_.fireThrC100 - cfg_.fireHysteresisC100));
        }
        if (newFire && !fire_) {
            fire_            = true;
            edges.fireRising = true;
        } else if (!newFire && fire_) {
            fire_             = false;
            edges.fireFalling = true;
        }
    }

    // --- Sensor fault (from anomaly flags, FR-04) ---
    const bool faultNow = ((snap.anomalyFlags & cfg::flag::kSensorOpen) != 0U) ||
                          ((snap.anomalyFlags & cfg::flag::kOneWireErr) != 0U);
    if (faultNow && !sensorFault_) {
        sensorFault_       = true;
        edges.sensorRising = true;
    } else if (!faultNow && sensorFault_) {
        sensorFault_        = false;
        edges.sensorFalling = true;
    }

    // --- Diff alarm (10-min averages, FR-07) ---
    // Direction depends on window_goal: CoolRoom fires when outside is cooler (diff < 0);
    // WarmRoom fires when outside is warmer (diff > 0).  Alarm clears if direction flips.
    const bool avgsValid = (snap.innerAvg != kTempInvalid) && (snap.outerAvg != kTempInvalid);
    if (avgsValid) {
        const int16_t diff    = static_cast<int16_t>(snap.outerAvg - snap.innerAvg);
        const int16_t absDiff = (diff < 0) ? static_cast<int16_t>(-diff) : diff;
        const auto    goal    = static_cast<cfg::window_advisor::Goal>(cfg_.windowGoal);
        const bool    dirMatch =
            (goal == cfg::window_advisor::Goal::kCoolRoom) ? (diff < 0) : (diff > 0);

        bool newDiff = false;
        if (!diff_) {
            newDiff = dirMatch && (absDiff >= cfg_.diffThresholdC100);
        } else {
            newDiff = dirMatch && (absDiff >= (cfg_.diffThresholdC100 - cfg_.diffHysteresisC100));
        }
        if (newDiff && !diff_) {
            diff_            = true;
            edges.diffRising = true;
        } else if (!newDiff && diff_) {
            diff_             = false;
            edges.diffFalling = true;
        }
    }

    return edges;
}

void AlarmState::reset() {
    fire_        = false;
    diff_        = false;
    sensorFault_ = false;
}
