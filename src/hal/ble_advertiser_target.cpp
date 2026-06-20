#include "hal/ble_advertiser.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "Config.h"

// This driver uses NimBLE *legacy* advertising; NimBLEDevice::getAdvertising()
// returns NimBLEAdvertising* only when extended advertising is disabled.
#if defined(CONFIG_BT_NIMBLE_EXT_ADV) && CONFIG_BT_NIMBLE_EXT_ADV
#error "ble_advertiser_target requires CONFIG_BT_NIMBLE_EXT_ADV = 0 (legacy advertising)"
#endif

// Non-connectable BLE advertising beacon over NimBLE-Arduino 2.x (runs on Core 0
// beside WiFi). The 9-byte manufacturer-data payload (§6.2) is built by the
// domain encoder and handed to setPayload(); this driver only transmits it.

namespace {
// BLE advertising interval is expressed in 0.625 ms units (ms * 8 / 5).
constexpr uint16_t msToAdvUnits(uint32_t ms) {
    return static_cast<uint16_t>((ms * 8U) / 5U);
}
} // namespace

Result<void> BleAdvertiser::init(const char* deviceName, uint16_t /*companyId*/) {
    // companyId travels inside the manufacturer-data payload (§6.2, set via
    // setPayload), so it is not needed here.
    if (!NimBLEDevice::init(deviceName != nullptr ? deviceName : cfg::ble::kDeviceName)) {
        return Result<void>::err(Status::kNotReady);
    }
    adv_ = NimBLEDevice::getAdvertising();
    if (adv_ == nullptr) {
        return Result<void>::err(Status::kNotReady);
    }
    adv_->setConnectableMode(BLE_GAP_CONN_MODE_NON); // beacon only, no connections
    adv_->setMinInterval(msToAdvUnits(cfg::ble::kAdvIntervalMinMs));
    adv_->setMaxInterval(msToAdvUnits(cfg::ble::kAdvIntervalMaxMs));
    return Result<void>::ok();
}

void BleAdvertiser::setPayload(const uint8_t* data, uint8_t len) {
    if (adv_ == nullptr || data == nullptr) {
        return;
    }
    // Clamp to the AD-payload budget (parity with the fake; keeps the legacy
    // 31-byte adv PDU from overflowing if a caller passes an oversized buffer).
    const uint8_t copyLen = (len < cfg::ble::kMaxPayloadBytes) ? len : cfg::ble::kMaxPayloadBytes;
    adv_->setManufacturerData(data, copyLen);
}

Result<void> BleAdvertiser::burst(uint8_t count, uint32_t spacingMs) {
    if (adv_ == nullptr) {
        return Result<void>::err(Status::kNotReady);
    }
    if (count == 0U) {
        return Result<void>::ok(); // nothing to send; start(0) would advertise forever
    }
    // Advertise for a bounded window covering `count` events, then auto-stop.
    // NimBLE times this internally, so the call does not block.
    const uint32_t durationMs = static_cast<uint32_t>(count) * spacingMs;
    if (!adv_->start(durationMs)) {
        return Result<void>::err(Status::kNotReady);
    }
    return Result<void>::ok();
}
