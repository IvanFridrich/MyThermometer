#include "hal/onewire_bus.h"

OneWireBus::OneWireBus(uint8_t /*pin*/) {}

Result<Temperature> OneWireBus::readCentiC() {
    if (nextStatus_ != Status::kOk) {
        return Result<Temperature>::err(nextStatus_);
    }
    return Result<Temperature>::ok(nextReading_);
}

Result<uint64_t> OneWireBus::readRomId() {
    lastRomId_ = nextRomId_;
    return Result<uint64_t>::ok(nextRomId_);
}
