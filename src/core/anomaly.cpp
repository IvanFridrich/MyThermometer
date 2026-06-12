#include "anomaly.h"

#include <cstdint>

#include "Config.h"
#include "result.h"
#include "types.h"

AnomalyDetector::AnomalyDetector(uint8_t consecutiveErrThreshold)
    : threshold_(consecutiveErrThreshold) {}

EventFlags AnomalyDetector::classify(const Result<Temperature>& reading) {
    EventFlags flags = 0;

    if (!reading.isOk()) {
        ++consecutiveErrors_;
        switch (reading.status()) {
        case Status::kSensorOpen:
            flags |= cfg::flag::kSensorOpen;
            break;
        case Status::kOneWireErr:
            flags |= cfg::flag::kOneWireErr;
            break;
        default:
            break;
        }
        if (consecutiveErrors_ >= threshold_) {
            flags |= cfg::flag::kSensorOpen;
        }
        return flags;
    }

    // Reading is OK — check plausibility.
    consecutiveErrors_  = 0;
    const Temperature t = reading.value();
    // OneWire sensors occasionally return INT16_MIN as a bus glitch even when
    // the CRC passes; treat it as a weird value rather than a hard sensor error.
    if (t == kTempInvalid) {
        flags |= cfg::flag::kWeirdValue;
        return flags;
    }
    const auto minC100 = static_cast<int16_t>(cfg::temp::kValidMinC * cfg::sample::kStoreScale);
    const auto maxC100 = static_cast<int16_t>(cfg::temp::kValidMaxC * cfg::sample::kStoreScale);
    const auto senC100 =
        static_cast<int16_t>(cfg::temp::kDisconnectSentinelC * cfg::sample::kStoreScale);
    if (t < minC100 || t > maxC100 || t == senC100) {
        flags |= cfg::flag::kWeirdValue;
    }
    return flags;
}

void AnomalyDetector::reset() {
    consecutiveErrors_ = 0;
}
