#pragma once
#include <cstdint>

#include "result.h"
#include "types.h"

// Classifies a single sensor reading into EventFlags.
// Tracks consecutive errors to raise SENSOR_OPEN after N failures.
class AnomalyDetector {
  public:
    // consecutiveErrThreshold: raise SENSOR_OPEN flag after this many
    // back-to-back non-kOk results (default 3, per FR-04).
    explicit AnomalyDetector(uint8_t consecutiveErrThreshold = 3);

    // Examine a Result<Temperature> returned by OneWireBus::readCentiC().
    // Returns a bitmask of cfg::flag::k* constants appropriate for this reading.
    EventFlags classify(const Result<Temperature>& reading);

    // Reset consecutive-error counter (e.g. after sensor re-connects).
    void reset();

    uint8_t consecutiveErrors() const { return consecutiveErrors_; }

  private:
    uint8_t threshold_;
    uint8_t consecutiveErrors_{0};
};
