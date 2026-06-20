#include "hal/wifi_hal.h"

#include <cstring>

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "Config.h"

// WiFi connection + mDNS over the Arduino WiFi stack (runs on Core 0).
// begin() only *initiates* the association; connection completes asynchronously
// and isConnected() reports status. The reconnect/backoff state machine
// (FR-15) lives in the app layer (web/supervisor task), which polls
// isConnected() and re-calls begin() with cfg::net::kReconnectMin/MaxMs.

Result<void> WifiHal::begin(const char* ssid, const char* password) {
    if (ssid == nullptr || password == nullptr) {
        return Result<void>::err(Status::kInvalidArg);
    }
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    return Result<void>::ok();
}

bool WifiHal::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

int8_t WifiHal::rssi() const {
    if (WiFi.status() != WL_CONNECTED) {
        return cfg::net::kRssiInvalid; // 0 dBm is a valid level; use a sentinel
    }
    return static_cast<int8_t>(WiFi.RSSI()); // dBm, ~-100..0 fits int8_t
}

void WifiHal::getIp(char* buf, uint8_t len) const {
    if (buf == nullptr || len == 0U) {
        return;
    }
    const String ip = WiFi.localIP().toString();
    std::strncpy(buf, ip.c_str(), len - 1U);
    buf[len - 1U] = '\0';
}

Result<void> WifiHal::startMdns(const char* hostname) {
    if (hostname == nullptr) {
        return Result<void>::err(Status::kInvalidArg);
    }
    // Idempotent: the app re-runs this on every reconnect (ADR-15), so tear down
    // any previous responder first to avoid duplicate service registrations.
    MDNS.end();
    if (!MDNS.begin(hostname)) {
        return Result<void>::err(Status::kNotReady);
    }
    if (!MDNS.addService("http", "tcp", cfg::net::kHttpPort)) {
        return Result<void>::err(Status::kNotReady);
    }
    return Result<void>::ok();
}
