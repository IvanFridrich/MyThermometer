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

AlarmEdges AlarmState::update(const MeasurementSnapshot& snap) {
    AlarmEdges edges{};
    updateFire(snap, edges);
    updateSensor(snap.anomalyFlags, edges);
    return edges;
}

void AlarmState::reset() {
    fire_        = false;
    sensorFault_ = false;
}
