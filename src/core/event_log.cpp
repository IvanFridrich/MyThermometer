#include "event_log.h"

#include <cstdint>
#include <cstring>

#include "Config.h"

static const LogEntry kEmptyEntry{};

void EventLog::append(cfg::log::Level level, const char* module, const char* msg,
                      uint32_t uptimeMs) {
    LogEntry& e = buf_[head_];
    e.uptimeMs  = uptimeMs;
    e.level     = level;
    std::strncpy(e.module, module != nullptr ? module : "", sizeof(e.module) - 1U);
    e.module[sizeof(e.module) - 1U] = '\0';
    std::strncpy(e.message, msg != nullptr ? msg : "", sizeof(e.message) - 1U);
    e.message[sizeof(e.message) - 1U] = '\0';

    head_ = static_cast<uint8_t>((head_ + 1U) % cfg::log::kEventLogCapacity);
    if (count_ < cfg::log::kEventLogCapacity) {
        ++count_;
    }
}

const LogEntry& EventLog::at(uint8_t idx) const {
    if (idx >= count_) {
        return kEmptyEntry;
    }
    const auto pos = static_cast<uint8_t>((head_ + cfg::log::kEventLogCapacity - count_ + idx) %
                                          cfg::log::kEventLogCapacity);
    return buf_[pos];
}

void EventLog::clear() {
    head_  = 0;
    count_ = 0;
}
