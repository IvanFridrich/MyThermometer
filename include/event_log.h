#pragma once
#include <array>
#include <cstdint>

#include "Config.h"

// One structured log entry (UART format: [+uptimeMs][LEVEL][module] message).
struct LogEntry {
    uint32_t        uptimeMs{0};
    cfg::log::Level level{cfg::log::Level::kInfo};
    char            module[8]{};
    char            message[48]{};
};

// In-memory circular log of the most recent cfg::log::kEventLogCapacity entries.
// Oldest entry is overwritten when full.
class EventLog {
  public:
    void    append(cfg::log::Level level, const char* module, const char* msg, uint32_t uptimeMs);
    uint8_t count() const { return count_; }
    const LogEntry& at(uint8_t idx) const; // 0 = oldest surviving entry
    void            clear();

  private:
    std::array<LogEntry, cfg::log::kEventLogCapacity> buf_{};
    uint8_t                                           head_{0};
    uint8_t                                           count_{0};
};
