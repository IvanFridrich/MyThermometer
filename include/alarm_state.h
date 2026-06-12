#pragma once
#include <cstdint>

#include "config_model.h"
#include "types.h"

// Edges detected during a single update() call.
struct AlarmEdges {
    bool fireRising    = false; // fire alarm newly SET
    bool fireFalling   = false; // fire alarm newly CLEARED
    bool diffRising    = false; // diff alarm newly SET
    bool diffFalling   = false; // diff alarm newly CLEARED
    bool sensorRising  = false; // sensor fault newly SET
    bool sensorFalling = false; // sensor fault newly CLEARED

    bool any() const {
        return fireRising || fireFalling || diffRising || diffFalling || sensorRising ||
               sensorFalling;
    }
};

// Snapshot passed to AlarmState::update() each measurement cycle.
struct MeasurementSnapshot {
    Temperature innerRaw;     // instantaneous inner reading (for fire detection)
    Temperature innerAvg;     // 10-min floating average
    Temperature outerAvg;     // 10-min floating average
    EventFlags  anomalyFlags; // flags from AnomalyDetector (SENSOR_OPEN, ONEWIRE_ERR, …)
};

// Stateful alarm evaluator with hysteresis for fire, diff, and sensor-fault conditions.
// Configuration thresholds are held by reference — caller must keep cfg alive.
class AlarmState {
  public:
    explicit AlarmState(const ConfigModel& cfg);

    // Evaluate snapshot against thresholds; return newly risen/fallen edges.
    AlarmEdges update(const MeasurementSnapshot& snap);

    bool isFire() const { return fire_; }
    bool isDiff() const { return diff_; }
    bool isSensorFault() const { return sensorFault_; }

    void reset();

  private:
    const ConfigModel& cfg_;
    bool               fire_{false};
    bool               diff_{false};
    bool               sensorFault_{false};
};
