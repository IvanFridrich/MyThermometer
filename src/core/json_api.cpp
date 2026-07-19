#include "json_api.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "Config.h"
#include "history_buffer.h"
#include "types.h"
#include "window_advice.h"

namespace json_api {

namespace {
constexpr size_t kNumBuf = 12; // fits any int16/uint16 decimal, the diff range, or "null"
constexpr size_t kRomBuf = 24; // "0x" + 16 hex digits + NUL

const char* boolStr(bool value) {
    return value ? "true" : "false";
}

const char* adviceStr(window::Advice advice) {
    switch (advice) {
    case window::Advice::kOpen:
        return "open";
    case window::Advice::kClose:
        return "close";
    case window::Advice::kNoChange:
        break;
    }
    return "nochange";
}

// Format a temperature as a decimal integer, or "null" when invalid.
void fmtTemp(char* out, size_t cap, Temperature t) {
    const int n = (t == kTempInvalid) ? std::snprintf(out, cap, "null")
                                      : std::snprintf(out, cap, "%d", static_cast<int>(t));
    if (n < 0 || static_cast<size_t>(n) >= cap) {
        out[0] = '\0';
    }
}
} // namespace

size_t serializeCurrent(const CurrentStatus& s, char* buf, size_t cap) {
    if (buf == nullptr || cap == 0U) {
        return 0;
    }

    std::array<char, kNumBuf> inner{};
    std::array<char, kNumBuf> outer{};
    std::array<char, kNumBuf> diff{};
    fmtTemp(inner.data(), inner.size(), s.innerC100);
    fmtTemp(outer.data(), outer.size(), s.outerC100);
    if (s.innerC100 == kTempInvalid || s.outerC100 == kTempInvalid) {
        fmtTemp(diff.data(), diff.size(), kTempInvalid); // emits "null"
    } else {
        const int dn = std::snprintf(diff.data(), diff.size(), "%d",
                                     static_cast<int>(s.outerC100 - s.innerC100));
        if (dn < 0 || static_cast<size_t>(dn) >= diff.size()) {
            diff[0] = '\0';
        }
    }

    std::array<char, kRomBuf> innerRom{};
    std::array<char, kRomBuf> outerRom{};
    int                       rn = std::snprintf(innerRom.data(), innerRom.size(), "0x%016llX",
                                                 static_cast<unsigned long long>(s.innerRom));
    if (rn < 0) {
        innerRom[0] = '\0';
    }
    rn = std::snprintf(outerRom.data(), outerRom.size(), "0x%016llX",
                       static_cast<unsigned long long>(s.outerRom));
    if (rn < 0) {
        outerRom[0] = '\0';
    }

    std::array<char, kNumBuf> tod{};
    if (s.todMin < 0) {
        std::snprintf(tod.data(), tod.size(), "null");
    } else {
        const int tn = std::snprintf(tod.data(), tod.size(), "%d", static_cast<int>(s.todMin));
        if (tn < 0 || static_cast<size_t>(tn) >= tod.size()) {
            tod[0] = '\0';
        }
    }

    const int n = std::snprintf(
        buf, cap,
        R"({"inner_c100":%s,"outer_c100":%s,"diff_c100":%s,"window":"%s",)"
        R"("window_goal":%u,"fire":%s,"sensor_fault":%s,"diff_alarm":%s,)"
        R"("uptime_s":%lu,"free_heap":%lu,"min_free_heap":%lu,"rssi":%d,)"
        R"("inner_rom":"%s","outer_rom":"%s","beeper":%s,"email":%s,)"
        R"("fire_thr_c100":%d,"fire_hyst_c100":%d,"diff_thr_c100":%d,)"
        R"("diff_hyst_c100":%d,"contrast":%u,)"
        R"("quiet_from_min":%d,"quiet_to_min":%d,"tod_min":%s})",
        inner.data(), outer.data(), diff.data(), adviceStr(s.windowAdvice),
        static_cast<unsigned>(s.windowGoal), boolStr(s.fire), boolStr(s.sensorFault),
        boolStr(s.diffAlarm), static_cast<unsigned long>(s.uptimeS),
        static_cast<unsigned long>(s.freeHeap), static_cast<unsigned long>(s.minFreeHeap),
        static_cast<int>(s.rssi), innerRom.data(), outerRom.data(), boolStr(s.beeperEnabled),
        boolStr(s.emailEnabled), static_cast<int>(s.fireThrC100), static_cast<int>(s.fireHystC100),
        static_cast<int>(s.diffThrC100), static_cast<int>(s.diffHystC100),
        static_cast<unsigned>(s.contrast), static_cast<int>(s.quietFromMin),
        static_cast<int>(s.quietToMin), tod.data());

    if (n < 0 || static_cast<size_t>(n) >= cap) {
        return 0;
    }
    return static_cast<size_t>(n);
}

size_t serializeHistory(const HistoryBuffer& hist, uint32_t uptimeS, char* buf, size_t cap) {
    if (buf == nullptr || cap == 0U) {
        return 0;
    }
    const uint16_t count   = hist.count();
    const uint32_t strideS = cfg::sample::kHistoryStrideMs / 1000UL;

    int n = std::snprintf(buf, cap, R"({"uptime_s":%lu,"stride_s":%lu,"count":%u,"samples":[)",
                          static_cast<unsigned long>(uptimeS), static_cast<unsigned long>(strideS),
                          static_cast<unsigned>(count));
    if (n < 0 || static_cast<size_t>(n) >= cap) {
        return 0;
    }
    auto len = static_cast<size_t>(n);

    for (uint16_t i = 0; i < count; ++i) {
        const HistoryRecord&      r = hist.at(i);
        std::array<char, kNumBuf> inner{};
        std::array<char, kNumBuf> outer{};
        fmtTemp(inner.data(), inner.size(), r.t_inner_c100);
        fmtTemp(outer.data(), outer.size(), r.t_outer_c100);
        n = std::snprintf(buf + len, cap - len, R"(%s{"i":%s,"o":%s,"f":%u})", (i == 0U) ? "" : ",",
                          inner.data(), outer.data(), static_cast<unsigned>(r.flags));
        if (n < 0 || static_cast<size_t>(n) >= cap - len) {
            return 0;
        }
        len += static_cast<size_t>(n);
    }

    n = std::snprintf(buf + len, cap - len, "]}");
    if (n < 0 || static_cast<size_t>(n) >= cap - len) {
        return 0;
    }
    len += static_cast<size_t>(n);
    return len;
}

} // namespace json_api
