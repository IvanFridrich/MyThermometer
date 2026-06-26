#pragma once
#include <array>
#include <cstdint>

#include "Config.h"
#include "types.h"

// One history record — 6 bytes packed (§6.1).
// Timestamp is NOT stored; the web frontend derives it from record position.
struct __attribute__((packed)) HistoryRecord {
    int16_t  t_inner_c100{kTempInvalid};
    int16_t  t_outer_c100{kTempInvalid};
    uint16_t flags{0};
};
static_assert(sizeof(HistoryRecord) == 6, "HistoryRecord must be 6 bytes");

// Static ring buffer of kCapacity HistoryRecords (1008 × 6 B = 6048 B @ 7d).
// append() overwrites the oldest record when full.
// at(0) returns the oldest record; at(count()-1) the newest.
class HistoryBuffer {
  public:
    static constexpr uint16_t kCapacity = cfg::sample::kHistoryDepth;

    void                 append(const HistoryRecord& record);
    const HistoryRecord& at(uint16_t idx) const;
    uint16_t             count() const { return count_; }
    void                 clear();

  private:
    std::array<HistoryRecord, kCapacity> buf_{};
    uint16_t                             head_{0};  // next write slot
    uint16_t                             count_{0}; // valid records in buf_
};
