// Phase 4 will replace this stub with the Arduino WiFi + ESPmDNS driver.
#include "hal/wifi_hal.h"

Result<void> WifiHal::begin(const char* /*ssid*/, const char* /*password*/) {
    return Result<void>::ok();
}
bool WifiHal::isConnected() const {
    return false;
}
int8_t WifiHal::rssi() const {
    return 0;
}
void WifiHal::getIp(char* buf, uint8_t len) const {
    if (buf != nullptr && len > 0U) {
        buf[0] = '\0';
    }
}
Result<void> WifiHal::startMdns(const char* /*hostname*/) {
    return Result<void>::ok();
}
