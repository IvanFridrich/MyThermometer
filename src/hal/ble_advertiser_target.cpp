// Phase 4 will replace this stub with the NimBLE advertising driver.
#include "hal/ble_advertiser.h"

Result<void> BleAdvertiser::init(const char* /*deviceName*/, uint16_t /*companyId*/) {
    return Result<void>::ok();
}
void         BleAdvertiser::setPayload(const uint8_t* /*data*/, uint8_t /*len*/) {}
Result<void> BleAdvertiser::burst(uint8_t /*count*/, uint32_t /*spacingMs*/) {
    return Result<void>::ok();
}
