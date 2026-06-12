#pragma once
#include <array>
#include <cstdint>

#include "types.h"

// Fixed-window moving average over the N most recent Temperature samples.
// Invalid samples (kTempInvalid) are excluded from the average.
// The template parameter N must be <= 255.
template <uint8_t N> class MovingAverage {
  public:
    // Push a new sample.  kTempInvalid samples are stored but skipped in average().
    void push(Temperature sample) {
        buf_[head_] = sample;
        head_       = static_cast<uint8_t>((head_ + 1U) % N);
        if (count_ < N) {
            ++count_;
        }
    }

    // Return average of valid samples, or kTempInvalid if none are valid.
    Temperature average() const {
        int32_t sum   = 0;
        uint8_t valid = 0;
        for (uint8_t i = 0; i < count_; ++i) {
            uint8_t idx = static_cast<uint8_t>((head_ + N - count_ + i) % N);
            if (buf_[idx] != kTempInvalid) {
                sum += buf_[idx];
                ++valid;
            }
        }
        return (valid > 0) ? static_cast<Temperature>(sum / valid) : kTempInvalid;
    }

    // Clear buffer; next average() will return kTempInvalid until samples arrive.
    void reset() {
        buf_.fill(kTempInvalid);
        head_  = 0;
        count_ = 0;
    }

    uint8_t validCount() const {
        uint8_t n = 0;
        for (uint8_t i = 0; i < count_; ++i) {
            uint8_t idx = static_cast<uint8_t>((head_ + N - count_ + i) % N);
            if (buf_[idx] != kTempInvalid) {
                ++n;
            }
        }
        return n;
    }

    bool    isFull() const { return count_ == N; }
    uint8_t capacity() const { return N; }

  private:
    std::array<Temperature, N> buf_{};
    uint8_t                    head_{0};
    uint8_t                    count_{0};
};
