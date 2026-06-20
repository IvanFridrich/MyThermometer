#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstring>
#include <string>

#include "Config.h"
#include "history_buffer.h"
#include "json_api.h"
#include "types.h"
#include "window_advice.h"

// Phase 4 domain test: JSON API serializers. Exact-string assertions over the
// /api/current and /api/history shapes, null-on-invalid temps, window-advice
// strings, ROM hex, ordering (oldest first), and bounded-buffer overflow.

namespace {
json_api::CurrentStatus sampleStatus() {
    json_api::CurrentStatus s;
    s.innerC100     = 2345;
    s.outerC100     = 2100;
    s.windowAdvice  = window::Advice::kOpen;
    s.windowGoal    = 0;
    s.fire          = false;
    s.sensorFault   = false;
    s.diffAlarm     = false;
    s.uptimeS       = 3600;
    s.freeHeap      = 150000;
    s.minFreeHeap   = 140000;
    s.rssi          = -67;
    s.innerRom      = 0x28FF001122334455ULL;
    s.outerRom      = 0x28AA00AABBCCDDEEULL;
    s.beeperEnabled = true;
    s.emailEnabled  = true;
    s.fireThrC100   = 4500;
    s.fireHystC100  = 200;
    s.diffThrC100   = 200;
    s.diffHystC100  = 50;
    s.contrast      = 128;
    return s;
}
} // namespace

TEST_CASE("/api/current serializes exactly and returns the written length") {
    char              buf[cfg::net::kJsonCurrentBufSize];
    const size_t      n = json_api::serializeCurrent(sampleStatus(), buf, sizeof(buf));
    const std::string expected =
        "{\"inner_c100\":2345,\"outer_c100\":2100,\"diff_c100\":-245,\"window\":\"open\","
        "\"window_goal\":0,\"fire\":false,\"sensor_fault\":false,\"diff_alarm\":false,"
        "\"uptime_s\":3600,\"free_heap\":150000,\"min_free_heap\":140000,\"rssi\":-67,"
        "\"inner_rom\":\"0x28FF001122334455\",\"outer_rom\":\"0x28AA00AABBCCDDEE\","
        "\"beeper\":true,\"email\":true,\"fire_thr_c100\":4500,\"fire_hyst_c100\":200,"
        "\"diff_thr_c100\":200,\"diff_hyst_c100\":50,\"contrast\":128}";
    CHECK(std::string(buf) == expected);
    CHECK(n == expected.size());
    CHECK(n == std::strlen(buf));
}

TEST_CASE("/api/current emits null for an invalid temperature and its diff") {
    json_api::CurrentStatus s = sampleStatus();
    s.innerC100               = kTempInvalid;
    char buf[cfg::net::kJsonCurrentBufSize];
    json_api::serializeCurrent(s, buf, sizeof(buf));
    const std::string out(buf);
    CHECK(out.find("\"inner_c100\":null") != std::string::npos);
    CHECK(out.find("\"outer_c100\":2100") != std::string::npos);
    CHECK(out.find("\"diff_c100\":null") != std::string::npos);
}

TEST_CASE("/api/current window advice maps to the expected strings") {
    json_api::CurrentStatus s = sampleStatus();
    char                    buf[cfg::net::kJsonCurrentBufSize];

    s.windowAdvice = window::Advice::kClose;
    json_api::serializeCurrent(s, buf, sizeof(buf));
    CHECK(std::string(buf).find("\"window\":\"close\"") != std::string::npos);

    s.windowAdvice = window::Advice::kNoChange;
    json_api::serializeCurrent(s, buf, sizeof(buf));
    CHECK(std::string(buf).find("\"window\":\"nochange\"") != std::string::npos);
}

TEST_CASE("/api/current returns 0 when the buffer is too small") {
    char buf[50];
    CHECK(json_api::serializeCurrent(sampleStatus(), buf, sizeof(buf)) == 0);
}

TEST_CASE("serializers reject a null buffer or zero capacity") {
    char          buf[64];
    HistoryBuffer hist;
    CHECK(json_api::serializeCurrent(sampleStatus(), nullptr, 64) == 0);
    CHECK(json_api::serializeCurrent(sampleStatus(), buf, 0) == 0);
    CHECK(json_api::serializeHistory(hist, 0, nullptr, 64) == 0);
    CHECK(json_api::serializeHistory(hist, 0, buf, 0) == 0);
}

TEST_CASE("/api/history serializes an empty buffer") {
    HistoryBuffer     hist;
    char              buf[cfg::net::kJsonHistoryBufSize];
    const size_t      n        = json_api::serializeHistory(hist, 0, buf, sizeof(buf));
    const std::string expected = "{\"uptime_s\":0,\"stride_s\":600,\"count\":0,\"samples\":[]}";
    CHECK(std::string(buf) == expected);
    CHECK(n == expected.size());
}

TEST_CASE("/api/history serializes records oldest-first with null temps") {
    HistoryBuffer hist;
    hist.append(HistoryRecord{100, 200, 1});
    hist.append(HistoryRecord{kTempInvalid, 400, 4});
    char buf[cfg::net::kJsonHistoryBufSize];
    json_api::serializeHistory(hist, 600, buf, sizeof(buf));
    const std::string expected = "{\"uptime_s\":600,\"stride_s\":600,\"count\":2,\"samples\":["
                                 "{\"i\":100,\"o\":200,\"f\":1},{\"i\":null,\"o\":400,\"f\":4}]}";
    CHECK(std::string(buf) == expected);
}

TEST_CASE("/api/history returns 0 when the buffer is too small") {
    HistoryBuffer hist;
    hist.append(HistoryRecord{100, 200, 1});
    char buf[16];
    CHECK(json_api::serializeHistory(hist, 600, buf, sizeof(buf)) == 0);
}

TEST_CASE("/api/history full buffer fits the maximum record count") {
    HistoryBuffer hist;
    for (uint16_t i = 0; i < HistoryBuffer::kCapacity; ++i) {
        // -32767 is the widest numeric form (INT16_MIN serializes shorter, as null).
        hist.append(HistoryRecord{-32767, -32767, 0xFFFF});
    }
    char         buf[cfg::net::kJsonHistoryBufSize];
    const size_t n = json_api::serializeHistory(hist, 4294967295UL, buf, sizeof(buf));
    CHECK(n > 0); // must fit within the configured buffer at full depth
    CHECK(n == std::strlen(buf));
    CHECK(std::string(buf).rfind("]}", std::string::npos) != std::string::npos);
}
