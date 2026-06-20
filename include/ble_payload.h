#pragma once
#include <array>
#include <cstdint>

#include "types.h"

// BLE manufacturer-specific advertising payload (SPECIFICATION.md §6.2).
// Built in the domain layer (host byte-exact testable) and handed to
// BleAdvertiser::setPayload() byte-for-byte. Little-endian, 9 bytes:
//   [0..1] company ID (LE)   [2] version   [3..4] T_inner (LE int16)
//   [5..6] T_outer (LE int16)  [7] flags    [8] seq
namespace ble_payload {

constexpr uint8_t kSize = 9;

// Flags byte (§6.2) bit positions — DISTINCT from the internal cfg::flag::* layout.
namespace bit {
constexpr uint8_t kDiffExceeded = 1U << 0;
constexpr uint8_t kFire         = 1U << 1;
constexpr uint8_t kSensorOpen   = 1U << 2;
constexpr uint8_t kOneWireErr   = 1U << 3;
constexpr uint8_t kInnerValid   = 1U << 4;
constexpr uint8_t kOuterValid   = 1U << 5;
} // namespace bit

// Encode the 9-byte payload. eventFlags is the internal cfg::flag::* bitfield;
// the four relevant condition bits are remapped to the §6.2 positions.
// INNER_VALID / OUTER_VALID are derived from the transmitted temperatures
// (kTempInvalid => bit cleared), so they always agree with the bytes sent.
[[nodiscard]] std::array<uint8_t, kSize> encode(Temperature tInner, Temperature tOuter,
                                                EventFlags eventFlags, uint8_t seq);

} // namespace ble_payload
