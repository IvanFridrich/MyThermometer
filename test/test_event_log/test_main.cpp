#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>

#include "Config.h"
#include "event_log.h"

// Phase 2 domain test: EventLog — in-RAM circular log of LogEntry.
// Covers append/read, field truncation (module[8], message[48]), null inputs,
// wrap-around at kEventLogCapacity, ordering and clear.

TEST_CASE("append stores all fields and is readable at index 0") {
    EventLog log;
    log.append(cfg::log::Level::kWarn, "WIFI", "link down", 12345);
    CHECK(log.count() == 1);
    CHECK(log.at(0).uptimeMs == 12345U);
    CHECK(log.at(0).level == cfg::log::Level::kWarn);
    CHECK(std::string(log.at(0).module) == "WIFI");
    CHECK(std::string(log.at(0).message) == "link down");
}

TEST_CASE("count increments per append") {
    EventLog log;
    CHECK(log.count() == 0);
    log.append(cfg::log::Level::kInfo, "A", "one", 1);
    log.append(cfg::log::Level::kInfo, "B", "two", 2);
    CHECK(log.count() == 2);
}

TEST_CASE("out-of-range index returns the empty entry") {
    EventLog log;
    log.append(cfg::log::Level::kInfo, "X", "y", 7);
    const LogEntry& e = log.at(5);
    CHECK(e.uptimeMs == 0U);
    CHECK(std::string(e.module).empty());
    CHECK(std::string(e.message).empty());
}

TEST_CASE("all log levels round-trip") {
    EventLog log;
    log.append(cfg::log::Level::kTrace, "M", "t", 1);
    log.append(cfg::log::Level::kError, "M", "e", 2);
    CHECK(log.at(0).level == cfg::log::Level::kTrace);
    CHECK(log.at(1).level == cfg::log::Level::kError);
}

// ---------------------------------------------------------------------------
// Truncation and null handling (no buffer overrun, always NUL-terminated)
// ---------------------------------------------------------------------------
TEST_CASE("a too-long module is truncated to 7 chars + NUL") {
    EventLog log;
    log.append(cfg::log::Level::kInfo, "ABCDEFGHIJ", "msg", 1); // 10 chars
    CHECK(std::string(log.at(0).module) == "ABCDEFG");          // 7 survivors
}

TEST_CASE("a module of exactly 7 chars is preserved") {
    EventLog log;
    log.append(cfg::log::Level::kInfo, "SENSORX", "msg", 1);
    CHECK(std::string(log.at(0).module) == "SENSORX");
}

TEST_CASE("a too-long message is truncated to 47 chars + NUL") {
    EventLog          log;
    const std::string longMsg(60, 'x');
    log.append(cfg::log::Level::kInfo, "M", longMsg.c_str(), 1);
    CHECK(std::string(log.at(0).message).size() == 47);
}

TEST_CASE("null module and message become empty strings") {
    EventLog log;
    log.append(cfg::log::Level::kInfo, nullptr, nullptr, 99);
    CHECK(log.count() == 1);
    CHECK(std::string(log.at(0).module).empty());
    CHECK(std::string(log.at(0).message).empty());
    CHECK(log.at(0).uptimeMs == 99U);
}

// ---------------------------------------------------------------------------
// Ring-buffer behaviour
// ---------------------------------------------------------------------------
TEST_CASE("fills exactly to capacity") {
    EventLog log;
    for (uint8_t i = 0; i < cfg::log::kEventLogCapacity; ++i) {
        log.append(cfg::log::Level::kInfo, "M", "m", i);
    }
    CHECK(log.count() == cfg::log::kEventLogCapacity);
    CHECK(log.at(0).uptimeMs == 0U); // first appended still oldest
    CHECK(log.at(cfg::log::kEventLogCapacity - 1).uptimeMs ==
          static_cast<uint32_t>(cfg::log::kEventLogCapacity - 1));
}

TEST_CASE("wrap-around evicts the oldest and caps the count") {
    EventLog log;
    // Append capacity + 1 entries with uptimeMs = index.
    for (uint16_t i = 0; i <= cfg::log::kEventLogCapacity; ++i) {
        log.append(cfg::log::Level::kInfo, "M", "m", i);
    }
    CHECK(log.count() == cfg::log::kEventLogCapacity); // capped
    CHECK(log.at(0).uptimeMs == 1U);                   // index 0 was evicted
    CHECK(log.at(cfg::log::kEventLogCapacity - 1).uptimeMs == cfg::log::kEventLogCapacity);
}

TEST_CASE("deep wrap keeps the last capacity entries in order") {
    EventLog       log;
    const uint16_t total = static_cast<uint16_t>(cfg::log::kEventLogCapacity * 3U);
    for (uint16_t i = 0; i < total; ++i) {
        log.append(cfg::log::Level::kInfo, "M", "m", i);
    }
    CHECK(log.count() == cfg::log::kEventLogCapacity);
    CHECK(log.at(0).uptimeMs == static_cast<uint32_t>(total - cfg::log::kEventLogCapacity));
    CHECK(log.at(cfg::log::kEventLogCapacity - 1).uptimeMs == static_cast<uint32_t>(total - 1));
}

TEST_CASE("clear empties the log and it is reusable") {
    EventLog log;
    log.append(cfg::log::Level::kInfo, "M", "m", 1);
    log.append(cfg::log::Level::kInfo, "M", "m", 2);
    log.clear();
    CHECK(log.count() == 0);
    CHECK(log.at(0).uptimeMs == 0U); // empty entry
    log.append(cfg::log::Level::kInfo, "NEW", "fresh", 500);
    CHECK(log.count() == 1);
    CHECK(std::string(log.at(0).module) == "NEW");
}
