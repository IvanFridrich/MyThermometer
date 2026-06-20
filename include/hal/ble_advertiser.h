#pragma once
#include <cstdint>

#include "Config.h"
#include "result.h"

#ifndef NATIVE_BUILD
class NimBLEAdvertising; // fwd-decl; target .cpp includes the full NimBLE header
#endif

// HAL: non-connectable BLE advertising beacon (manufacturer-specific data §6.2).
// Target: NimBLE (Phase 4).  Fake: records last payload and burst count.
class BleAdvertiser {
  public:
    Result<void> init(const char* deviceName, uint16_t companyId);
    void         setPayload(const uint8_t* data, uint8_t len);

    // Send `count` advertising bursts separated by spacingMs each.
    Result<void> burst(uint8_t count, uint32_t spacingMs);

#ifdef NATIVE_BUILD
    // Inspection API
    bool           initialized() const { return initialized_; }
    const uint8_t* lastPayload() const { return lastPayload_; }
    uint8_t        lastPayloadLen() const { return lastPayloadLen_; }
    uint32_t       totalBurstCount() const { return totalBursts_; }

  private:
    bool     initialized_{false};
    uint8_t  lastPayload_[cfg::ble::kMaxPayloadBytes]{};
    uint8_t  lastPayloadLen_{0};
    uint32_t totalBursts_{0};
#endif

#ifndef NATIVE_BUILD
  private:
    NimBLEAdvertising* adv_{nullptr}; // owned by NimBLEDevice singleton
#endif
};
