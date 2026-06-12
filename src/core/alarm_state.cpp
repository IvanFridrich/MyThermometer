#include "alarm_state.h"

#include <cstdint>

#include "Config.h"
#include "config_model.h"
#include "types.h"

AlarmState::AlarmState(const ConfigModel& cfg) : cfg_(cfg) {}

void AlarmState::updateFire(const MeasurementSnapshot& snap, AlarmEdges& out) {
    if (snap.innerRaw == kTempInvalid) {
        return;
    }
    bool newFire = false;
    if (!fire_) {
        newFire = (snap.innerRaw >= cfg_.fireThrC100);
    } else {
        newFire = (snap.innerRaw >= (cfg_.fireThrC100 - cfg_.fireHysteresisC100));
    }
    if (newFire && !fire_) {
        fire_          = true;
        out.fireRising = true;
    } else if (!newFire && fire_) {
        fire_           = false;
        out.fireFalling = true;
    }
}

void AlarmState::updateSensor(EventFlags anomalyFlags, AlarmEdges& out) {
    const bool faultNow = ((anomalyFlags & cfg::flag::kSensorOpen) != 0U) ||
                          ((anomalyFlags & cfg::flag::kOneWireErr) != 0U);
    if (faultNow && !sensorFault_) {
        sensorFault_     = true;
        out.sensorRising = true;
    } else if (!faultNow && sensorFault_) {
        sensorFault_      = false;
        out.sensorFalling = true;
    }
}

void AlarmState::updateDiff(const MeasurementSnapshot& snap, AlarmEdges& out) {
    if (snap.innerAvg == kTempInvalid || snap.outerAvg == kTempInvalid) {
        return;
    }
    const auto diff     = static_cast<int16_t>(snap.outerAvg - snap.innerAvg);
    const auto absDiff  = (diff < 0) ? static_cast<int16_t>(-diff) : diff;
    const auto goal     = static_cast<cfg::window_advisor::Goal>(cfg_.windowGoal);
    const bool dirMatch = (goal == cfg::window_advisor::Goal::kCoolRoom) ? (diff < 0) : (diff > 0);

    const int16_t threshold =
        diff_ ? static_cast<int16_t>(cfg_.diffThresholdC100 - cfg_.diffHysteresisC100)
              : cfg_.diffThresholdC100;
    const bool newDiff = dirMatch && (absDiff >= threshold);

    if (newDiff && !diff_) {
        diff_          = true;
        out.diffRising = true;
    } else if (!newDiff && diff_) {
        diff_           = false;
        out.diffFalling = true;
    }
}

AlarmEdges AlarmState::update(const MeasurementSnapshot& snap) {
    AlarmEdges edges{};
    updateFire(snap, edges);
    updateSensor(snap.anomalyFlags, edges);
    updateDiff(snap, edges);
    return edges;
}

void AlarmState::reset() {
    fire_        = false;
    diff_        = false;
    sensorFault_ = false;
}
