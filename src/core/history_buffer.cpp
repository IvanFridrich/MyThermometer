#include "history_buffer.h"

// Phase 2 implements the ring-buffer logic (append, at, clear).

void HistoryBuffer::append(const HistoryRecord& record) {
    buf_[head_] = record;
    head_       = static_cast<uint16_t>((head_ + 1U) % kCapacity);
    if (count_ < kCapacity) {
        ++count_;
    }
}

const HistoryRecord& HistoryBuffer::at(uint16_t idx) const {
    static const HistoryRecord kEmpty{};
    if (idx >= count_) {
        return kEmpty;
    }
    uint16_t pos = static_cast<uint16_t>((head_ + kCapacity - count_ + idx) % kCapacity);
    return buf_[pos];
}

void HistoryBuffer::clear() {
    head_  = 0;
    count_ = 0;
}
