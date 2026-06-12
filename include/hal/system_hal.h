#pragma once
#include <cstdint>

// Cause of the last reset — used after boot to detect brownout / WDT events.
enum class ResetReason : uint8_t {
    kPowerOn = 0,
    kBrownout,
    kWatchdog,
    kSoftware,
    kPanic,
    kUnknown,
};

// HAL: system utilities — heap info, WDT feed, restart, reset reason.
// Target: ESP-IDF / Arduino (Phase 6).  Fake: injectable state + call counters.
class SystemHal {
  public:
    void        restart();
    uint32_t    freeHeap() const;
    uint32_t    minFreeHeap() const;
    void        wdtFeed();
    ResetReason resetReason() const;

#ifdef NATIVE_BUILD
    // Injection / inspection API
    void setFreeHeap(uint32_t h) { freeHeap_ = h; }
    void setMinFreeHeap(uint32_t h) { minFreeHeap_ = h; }
    void setResetReason(ResetReason r) { resetReason_ = r; }

    uint32_t wdtFeedCount() const { return wdtFeeds_; }
    uint32_t restartCount() const { return restarts_; }

  private:
    uint32_t    freeHeap_{200000};
    uint32_t    minFreeHeap_{180000};
    ResetReason resetReason_{ResetReason::kPowerOn};
    uint32_t    wdtFeeds_{0};
    uint32_t    restarts_{0};
#endif
};
