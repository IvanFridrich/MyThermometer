#pragma once
#include <cstdint>

#include "result.h"

// HAL: WiFi connection management + mDNS.
// Target: Arduino WiFi + ESPmDNS (Phase 4).  Fake: injectable connection state.
class WifiHal {
  public:
    Result<void> begin(const char* ssid, const char* password);
    bool         isConnected() const;
    int8_t       rssi() const;
    void         getIp(char* buf, uint8_t len) const;
    Result<void> startMdns(const char* hostname);

#ifdef NATIVE_BUILD
    // Injection API
    void        setConnected(bool c) { connected_ = c; }
    void        setRssi(int8_t r) { rssi_ = r; }
    void        setIp(const char* ip);
    const char* mdnsHostname() const { return mdnsHostname_; }

  private:
    bool   connected_{false};
    int8_t rssi_{-70};
    char   ip_[16]{"192.168.1.100"};
    char   mdnsHostname_[32]{};
#endif
};
