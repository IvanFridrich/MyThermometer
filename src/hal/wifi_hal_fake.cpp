#include "hal/wifi_hal.h"

#include <cstdint>
#include <cstring>

#include "result.h"

Result<void> WifiHal::begin(const char* /*ssid*/, const char* /*password*/) {
    connected_ = true;
    return Result<void>::ok();
}

bool WifiHal::isConnected() const {
    return connected_;
}
int8_t WifiHal::rssi() const {
    return rssi_;
}

void WifiHal::getIp(char* buf, uint8_t len) const {
    if (buf != nullptr && len > 0U) {
        std::strncpy(buf, ip_, len - 1U);
        buf[len - 1U] = '\0';
    }
}

Result<void> WifiHal::startMdns(const char* hostname) {
    if (hostname != nullptr) {
        std::strncpy(mdnsHostname_, hostname, sizeof(mdnsHostname_) - 1U);
        mdnsHostname_[sizeof(mdnsHostname_) - 1U] = '\0';
    }
    return Result<void>::ok();
}

void WifiHal::setIp(const char* ip) {
    if (ip != nullptr) {
        std::strncpy(ip_, ip, sizeof(ip_) - 1U);
        ip_[sizeof(ip_) - 1U] = '\0';
    }
}
