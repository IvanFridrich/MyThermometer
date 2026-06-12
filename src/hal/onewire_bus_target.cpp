// Phase 3 will replace this stub with the RMT-based DS18B20 driver.
#include "hal/onewire_bus.h"

static uint8_t g_pin;
OneWireBus::OneWireBus(uint8_t pin) {
    g_pin = pin;
}

Result<Temperature> OneWireBus::readCentiC() {
    return Result<Temperature>::err(Status::kNotReady);
}

Result<uint64_t> OneWireBus::readRomId() {
    return Result<uint64_t>::err(Status::kNotReady);
}
