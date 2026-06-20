#include "ble_payload.h"

#include <array>
#include <cstdint>

#include "Config.h"
#include "types.h"

namespace ble_payload {

namespace {
// Remap the internal cfg::flag::* condition bits onto the §6.2 flags byte and add
// the validity bits derived from the temperatures actually being transmitted.
uint8_t mapFlags(Temperature tInner, Temperature tOuter, EventFlags ev) {
    uint8_t f = 0;
    if ((ev & cfg::flag::kDiffExceeded) != 0U) {
        f |= bit::kDiffExceeded;
    }
    if ((ev & cfg::flag::kFire) != 0U) {
        f |= bit::kFire;
    }
    if ((ev & cfg::flag::kSensorOpen) != 0U) {
        f |= bit::kSensorOpen;
    }
    if ((ev & cfg::flag::kOneWireErr) != 0U) {
        f |= bit::kOneWireErr;
    }
    if (tInner != kTempInvalid) {
        f |= bit::kInnerValid;
    }
    if (tOuter != kTempInvalid) {
        f |= bit::kOuterValid;
    }
    return f;
}

constexpr uint16_t kByteMask = 0xFFU;
constexpr uint8_t  kByteBits = 8U;
} // namespace

std::array<uint8_t, kSize> encode(Temperature tInner, Temperature tOuter, EventFlags eventFlags,
                                  uint8_t seq) {
    const auto inner = static_cast<uint16_t>(tInner); // two's-complement bit pattern
    const auto outer = static_cast<uint16_t>(tOuter);

    std::array<uint8_t, kSize> out{};
    out[0] = static_cast<uint8_t>(cfg::ble::kCompanyId & kByteMask); // company ID, LE
    out[1] = static_cast<uint8_t>((cfg::ble::kCompanyId >> kByteBits) & kByteMask);
    out[2] = cfg::ble::kPayloadVersion;
    out[3] = static_cast<uint8_t>(inner & kByteMask); // T_inner int16, LE
    out[4] = static_cast<uint8_t>((inner >> kByteBits) & kByteMask);
    out[5] = static_cast<uint8_t>(outer & kByteMask); // T_outer int16, LE
    out[6] = static_cast<uint8_t>((outer >> kByteBits) & kByteMask);
    out[7] = mapFlags(tInner, tOuter, eventFlags);
    out[8] = seq;
    return out;
}

} // namespace ble_payload
