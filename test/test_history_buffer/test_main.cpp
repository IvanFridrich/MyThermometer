#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "Config.h"
#include "history_buffer.h"
#include "types.h"

// Phase 2 domain test: HistoryBuffer — static ring buffer of HistoryRecord.
// Covers empty access, ordering (at(0)=oldest), wrap-around at kCapacity,
// flag OR semantics, clear, and the packed 6-byte record contract (§6.1).

TEST_CASE("packed record is exactly 6 bytes") {
    CHECK(sizeof(HistoryRecord) == 6);
}

TEST_CASE("empty buffer reports zero and returns the empty record") {
    HistoryBuffer buf;
    CHECK(buf.count() == 0);
    // Out-of-range access returns a default (invalid) record, never UB.
    const HistoryRecord& r = buf.at(0);
    CHECK(r.t_inner_c100 == kTempInvalid);
    CHECK(r.t_outer_c100 == kTempInvalid);
    CHECK(r.flags == 0);
}

TEST_CASE("single append is readable at index 0") {
    HistoryBuffer buf;
    buf.append(HistoryRecord{2300, 2100, cfg::flag::kBoot});
    CHECK(buf.count() == 1);
    CHECK(buf.at(0).t_inner_c100 == 2300);
    CHECK(buf.at(0).t_outer_c100 == 2100);
    CHECK(buf.at(0).flags == cfg::flag::kBoot);
}

TEST_CASE("at(0) is oldest, at(count-1) is newest") {
    HistoryBuffer buf;
    buf.append(HistoryRecord{100, 100, 0});
    buf.append(HistoryRecord{200, 200, 0});
    buf.append(HistoryRecord{300, 300, 0});
    CHECK(buf.count() == 3);
    CHECK(buf.at(0).t_inner_c100 == 100); // oldest
    CHECK(buf.at(1).t_inner_c100 == 200);
    CHECK(buf.at(2).t_inner_c100 == 300); // newest
}

TEST_CASE("out-of-range index returns the empty record") {
    HistoryBuffer buf;
    buf.append(HistoryRecord{500, 400, 0});
    CHECK(buf.at(1).t_inner_c100 == kTempInvalid); // only index 0 valid
    CHECK(buf.at(999).t_inner_c100 == kTempInvalid);
}

TEST_CASE("fills exactly to capacity without wrapping") {
    HistoryBuffer buf;
    for (uint16_t i = 0; i < HistoryBuffer::kCapacity; ++i) {
        buf.append(HistoryRecord{static_cast<Temperature>(i), 0, 0});
    }
    CHECK(buf.count() == HistoryBuffer::kCapacity);
    CHECK(buf.at(0).t_inner_c100 == 0); // first appended still oldest
    CHECK(buf.at(HistoryBuffer::kCapacity - 1).t_inner_c100 ==
          static_cast<Temperature>(HistoryBuffer::kCapacity - 1));
}

TEST_CASE("wrap-around evicts the oldest and caps count at capacity") {
    HistoryBuffer buf;
    // Append capacity + 1 records; the very first must be evicted.
    for (uint16_t i = 0; i <= HistoryBuffer::kCapacity; ++i) {
        buf.append(HistoryRecord{static_cast<Temperature>(i), 0, 0});
    }
    CHECK(buf.count() == HistoryBuffer::kCapacity); // capped
    // Oldest surviving record is index 1 of the original sequence (value 1).
    CHECK(buf.at(0).t_inner_c100 == 1);
    // Newest is the last appended (value == kCapacity).
    CHECK(buf.at(HistoryBuffer::kCapacity - 1).t_inner_c100 ==
          static_cast<Temperature>(HistoryBuffer::kCapacity));
}

TEST_CASE("deep wrap keeps the last kCapacity records in order") {
    HistoryBuffer  buf;
    const uint16_t total = static_cast<uint16_t>(HistoryBuffer::kCapacity * 3U);
    for (uint16_t i = 0; i < total; ++i) {
        buf.append(HistoryRecord{static_cast<Temperature>(i % 1000), 0, 0});
    }
    CHECK(buf.count() == HistoryBuffer::kCapacity);
    // Oldest surviving value corresponds to (total - kCapacity).
    const auto firstSurvivor = static_cast<Temperature>((total - HistoryBuffer::kCapacity) % 1000);
    CHECK(buf.at(0).t_inner_c100 == firstSurvivor);
    const auto newest = static_cast<Temperature>((total - 1) % 1000);
    CHECK(buf.at(HistoryBuffer::kCapacity - 1).t_inner_c100 == newest);
}

TEST_CASE("flags are stored verbatim per record (OR happens upstream)") {
    HistoryBuffer  buf;
    const uint16_t combined = cfg::flag::kFire | cfg::flag::kDiffExceeded | cfg::flag::kWeirdValue;
    buf.append(HistoryRecord{4500, 2000, combined});
    CHECK(buf.at(0).flags == combined);
}

TEST_CASE("invalid temperatures round-trip through the record") {
    HistoryBuffer buf;
    buf.append(HistoryRecord{kTempInvalid, kTempInvalid, cfg::flag::kInnerInvalid});
    CHECK(buf.at(0).t_inner_c100 == kTempInvalid);
    CHECK(buf.at(0).flags == cfg::flag::kInnerInvalid);
}

TEST_CASE("clear empties the buffer") {
    HistoryBuffer buf;
    buf.append(HistoryRecord{100, 100, 0});
    buf.append(HistoryRecord{200, 200, 0});
    buf.clear();
    CHECK(buf.count() == 0);
    CHECK(buf.at(0).t_inner_c100 == kTempInvalid);
    // Reusable after clear.
    buf.append(HistoryRecord{900, 800, 0});
    CHECK(buf.count() == 1);
    CHECK(buf.at(0).t_inner_c100 == 900);
}

TEST_CASE("negative temperatures survive storage") {
    HistoryBuffer buf;
    buf.append(HistoryRecord{-1234, -2500, 0}); // -12.34 / -25.00 C
    CHECK(buf.at(0).t_inner_c100 == -1234);
    CHECK(buf.at(0).t_outer_c100 == -2500);
}
