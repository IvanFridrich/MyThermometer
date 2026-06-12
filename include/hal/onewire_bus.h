#pragma once
#include <cstdint>

#include "result.h"
#include "types.h"

// HAL: DS18B20 on a single-device OneWire bus.
// Target: RMT-based driver (Phase 3).  Fake: injectable test double.
class OneWireBus {
  public:
    explicit OneWireBus(uint8_t pin);

    // Read temperature in centi-°C.  Errors: kSensorOpen, kOneWireErr, kWeirdValue.
    Result<Temperature> readCentiC();

    // Read 64-bit ROM identifier of the single device on the bus.
    Result<uint64_t> readRomId();

#ifdef NATIVE_BUILD
  public:
    // Injection API — used by unit tests to control fake behaviour.
    void setNextReading(Temperature t, Status s = Status::kOk) {
        nextReading_ = t;
        nextStatus_  = s;
    }
    void     setNextRomId(uint64_t id) { nextRomId_ = id; }
    uint64_t lastQueriedRomId() const { return lastRomId_; }

  private:
    Temperature nextReading_{2300};
    Status      nextStatus_{Status::kOk};
    uint64_t    nextRomId_{0x2800000000000000ULL};
    uint64_t    lastRomId_{0};
#endif
};
