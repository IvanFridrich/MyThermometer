#include "hal/ble_advertiser.h"

#include <cstdint>
#include <cstring>

#include "Config.h"
#include "result.h"

Result<void> BleAdvertiser::init(const char* /*deviceName*/, uint16_t /*companyId*/) {
    initialized_ = true;
    return Result<void>::ok();
}

void BleAdvertiser::setPayload(const uint8_t* data, uint8_t len) {
    if (data == nullptr) {
        return;
    }
    const uint8_t copyLen = (len < cfg::ble::kMaxPayloadBytes) ? len : cfg::ble::kMaxPayloadBytes;
    std::memcpy(lastPayload_, data, copyLen);
    lastPayloadLen_ = copyLen;
}

Result<void> BleAdvertiser::burst(uint8_t count, uint32_t /*spacingMs*/) {
    totalBursts_ += count;
    return Result<void>::ok();
}
