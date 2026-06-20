#pragma once
#include <cstddef>
#include <cstdint>

#include "history_buffer.h"
#include "types.h"
#include "window_advice.h"

// JSON serializers for the HTTP API (wifi-https-server-nvs skill). Pure domain
// code: the app layer gathers the values, these build the response into a
// caller-provided bounded buffer (no dynamic allocation, no float formatting —
// temperatures are centi-°C integers; the browser divides). Invalid temperatures
// (kTempInvalid) are emitted as JSON null so the frontend renders gaps.
namespace json_api {

// Snapshot of everything GET /api/current reports. Gathered by the web task.
struct CurrentStatus {
    Temperature    innerC100{kTempInvalid};
    Temperature    outerC100{kTempInvalid};
    window::Advice windowAdvice{window::Advice::kNoChange};
    uint8_t        windowGoal{0};
    bool           fire{false};
    bool           sensorFault{false};
    bool           diffAlarm{false};
    uint32_t       uptimeS{0};
    uint32_t       freeHeap{0};
    uint32_t       minFreeHeap{0};
    int8_t         rssi{0};
    uint64_t       innerRom{0};
    uint64_t       outerRom{0};
    bool           beeperEnabled{false};
    bool           emailEnabled{false};
    int16_t        fireThrC100{0};
    int16_t        fireHystC100{0};
    int16_t        diffThrC100{0};
    int16_t        diffHystC100{0};
    uint8_t        contrast{0};
};

// Build GET /api/current into buf[cap]. Returns bytes written (excluding the NUL)
// or 0 if it would not fit. diff_c100 = outer - inner, null if either invalid.
size_t serializeCurrent(const CurrentStatus& s, char* buf, size_t cap);

// Build GET /api/history into buf[cap]: uptime_s (axis anchor), stride_s, count,
// and samples[] oldest-first, each {"i":<inC100|null>,"o":<outC100|null>,"f":flags}.
// Returns bytes written (excluding the NUL) or 0 if it would not fit.
size_t serializeHistory(const HistoryBuffer& hist, uint32_t uptimeS, char* buf, size_t cap);

} // namespace json_api
