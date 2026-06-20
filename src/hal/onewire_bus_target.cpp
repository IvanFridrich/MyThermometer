#include "hal/onewire_bus.h"

#include <array>

#include <Arduino.h>
#include <OneWire.h>

#include "Config.h"

// DS18B20 driver over the bit-banged paulstoffregen/OneWire library.
//
// NFR-08 deviation (owner-approved, see docs/adr-phase3.md ADR-10): the spec
// mandates RMT timing; this build uses the bit-banged library instead. The
// IRQ-disable during bit timing is isolated to Core 1 (cfg::task::kCoreApp),
// away from the WiFi/BLE stacks on Core 0.
//
// Conversion model: 12-bit conversion takes ~750 ms, so we never block for it.
// Each readCentiC() reads the scratchpad produced by the conversion started on
// the *previous* call, then kicks off the next conversion. The measurement task
// calls this once per minute, so data is always ready by the next call. The very
// first call returns kNotReady.

namespace {
constexpr uint8_t kCmdConvertT     = 0x44;
constexpr uint8_t kCmdReadScratch  = 0xBE;
constexpr uint8_t kCmdReadRom      = 0x33; // valid only with one device on the bus
constexpr uint8_t kScratchpadBytes = 9;
constexpr uint8_t kRomBytes        = 8;
constexpr uint8_t kCrcDataBytesSp  = 8;   // scratchpad CRC covers bytes [0..7]
constexpr uint8_t kCrcDataBytesRom = 7;   // ROM CRC covers bytes [0..6]
constexpr int32_t kCentiPerDegree  = 100; // centi-degC = raw * 100 / 16
constexpr int32_t kRawLsbPer16     = 16;  // DS18B20 LSB = 1/16 degC
} // namespace

OneWireBus::OneWireBus(uint8_t pin) : ow_(pin) {}

Result<Temperature> OneWireBus::readCentiC() {
    if (!convStarted_) {
        // No conversion in flight yet: start one; its result is read next call.
        if (ow_.reset() == 0) {
            return Result<Temperature>::err(Status::kSensorOpen);
        }
        ow_.skip();
        ow_.write(kCmdConvertT);
        convStarted_ = true;
        return Result<Temperature>::err(Status::kNotReady);
    }

    // Read the scratchpad from the conversion started on the previous call.
    if (ow_.reset() == 0) {
        convStarted_ = false; // device vanished; restart the sequence next call
        return Result<Temperature>::err(Status::kSensorOpen);
    }
    ow_.skip();
    ow_.write(kCmdReadScratch);
    std::array<uint8_t, kScratchpadBytes> sp{};
    for (uint8_t i = 0; i < kScratchpadBytes; ++i) {
        sp[i] = ow_.read();
    }

    // Immediately start the next conversion so the next call has fresh data.
    const bool present = (ow_.reset() != 0);
    if (present) {
        ow_.skip();
        ow_.write(kCmdConvertT);
    }
    convStarted_ = present;

    if (OneWire::crc8(sp.data(), kCrcDataBytesSp) != sp[kCrcDataBytesSp]) {
        return Result<Temperature>::err(Status::kOneWireErr);
    }
    // A floating line that still pulses presence can return all-zero bytes,
    // which pass the trivial CRC(0)=0 check; reject that explicitly.
    bool allZero = true;
    for (uint8_t i = 0; i < kScratchpadBytes; ++i) {
        if (sp[i] != 0U) {
            allZero = false;
            break;
        }
    }
    if (allZero) {
        return Result<Temperature>::err(Status::kOneWireErr);
    }

    const auto raw = static_cast<int16_t>((static_cast<uint16_t>(sp[1]) << 8U) | sp[0]);
    const auto centi =
        static_cast<Temperature>((static_cast<int32_t>(raw) * kCentiPerDegree) / kRawLsbPer16);
    return Result<Temperature>::ok(centi);
}

Result<uint64_t> OneWireBus::readRomId() {
    // This issues its own bus reset, which aborts any conversion readCentiC()
    // started; force the read-previous/trigger-next sequence to cold-restart.
    convStarted_ = false;
    if (ow_.reset() == 0) {
        return Result<uint64_t>::err(Status::kSensorOpen);
    }
    ow_.write(kCmdReadRom);
    std::array<uint8_t, kRomBytes> rom{};
    for (uint8_t i = 0; i < kRomBytes; ++i) {
        rom[i] = ow_.read();
    }
    if (OneWire::crc8(rom.data(), kCrcDataBytesRom) != rom[kCrcDataBytesRom]) {
        return Result<uint64_t>::err(Status::kOneWireErr);
    }
    uint64_t id = 0;
    for (uint8_t i = 0; i < kRomBytes; ++i) {
        id |= static_cast<uint64_t>(rom[i]) << (8U * i);
    }
    return Result<uint64_t>::ok(id);
}
