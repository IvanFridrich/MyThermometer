#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <array>
#include <cstdint>

#include "Config.h"
#include "ble_payload.h"
#include "types.h"

// Phase 4 domain test: BLE §6.2 manufacturer-data encoder. Byte-exact assertions
// over little-endian layout, two's-complement negative temps, validity bits
// derived from kTempInvalid, the cfg::flag::* -> §6.2 flags remap, and seq wrap.

namespace {
using Payload = std::array<uint8_t, ble_payload::kSize>;
} // namespace

TEST_CASE("payload is 9 bytes with company ID, version and seq in place") {
    const Payload p = ble_payload::encode(2345, 2100, 0, 42);
    CHECK(p.size() == 9);
    CHECK(p[0] == 0xFF); // company ID 0xFFFF, LE low
    CHECK(p[1] == 0xFF); // company ID, LE high
    CHECK(p[2] == cfg::ble::kPayloadVersion);
    CHECK(p[2] == 0x01);
    CHECK(p[8] == 42); // seq
}

TEST_CASE("positive temperatures are little-endian int16") {
    const Payload p = ble_payload::encode(2345, 2100, 0, 0);
    // 2345 = 0x0929 -> LE 0x29,0x09 ; 2100 = 0x0834 -> LE 0x34,0x08
    CHECK(p[3] == 0x29);
    CHECK(p[4] == 0x09);
    CHECK(p[5] == 0x34);
    CHECK(p[6] == 0x08);
}

TEST_CASE("negative temperatures are two's-complement little-endian") {
    const Payload p = ble_payload::encode(-1234, -2500, 0, 0);
    // -1234 = 0xFB2E -> LE 0x2E,0xFB ; -2500 = 0xF63C -> LE 0x3C,0xF6
    CHECK(p[3] == 0x2E);
    CHECK(p[4] == 0xFB);
    CHECK(p[5] == 0x3C);
    CHECK(p[6] == 0xF6);
}

TEST_CASE("a fully specified packet matches byte for byte") {
    const EventFlags ev = cfg::flag::kFire | cfg::flag::kDiffExceeded;
    const Payload    p  = ble_payload::encode(2345, 2100, ev, 42);
    const Payload    expected{0xFF, 0xFF, 0x01, 0x29, 0x09, 0x34, 0x08, 0x33, 0x2A};
    CHECK(p == expected);
}

TEST_CASE("validity bits track the transmitted temperatures") {
    SUBCASE("both valid") {
        const Payload p = ble_payload::encode(2000, 2000, 0, 0);
        CHECK((p[7] & ble_payload::bit::kInnerValid) != 0U);
        CHECK((p[7] & ble_payload::bit::kOuterValid) != 0U);
    }
    SUBCASE("inner invalid") {
        const Payload p = ble_payload::encode(kTempInvalid, 2000, 0, 0);
        CHECK((p[7] & ble_payload::bit::kInnerValid) == 0U);
        CHECK((p[7] & ble_payload::bit::kOuterValid) != 0U);
        // kTempInvalid = INT16_MIN = 0x8000 -> LE 0x00,0x80
        CHECK(p[3] == 0x00);
        CHECK(p[4] == 0x80);
    }
    SUBCASE("outer invalid") {
        const Payload p = ble_payload::encode(2000, kTempInvalid, 0, 0);
        CHECK((p[7] & ble_payload::bit::kInnerValid) != 0U);
        CHECK((p[7] & ble_payload::bit::kOuterValid) == 0U);
    }
}

TEST_CASE("condition flags are remapped from cfg::flag to the §6.2 positions") {
    const EventFlags ev = cfg::flag::kDiffExceeded | cfg::flag::kFire | cfg::flag::kSensorOpen |
                          cfg::flag::kOneWireErr;
    const Payload p = ble_payload::encode(2000, 2000, ev, 0);
    CHECK((p[7] & ble_payload::bit::kDiffExceeded) != 0U);
    CHECK((p[7] & ble_payload::bit::kFire) != 0U);
    CHECK((p[7] & ble_payload::bit::kSensorOpen) != 0U);
    CHECK((p[7] & ble_payload::bit::kOneWireErr) != 0U);
    // All four conditions + both validity bits = 0x3F.
    CHECK(p[7] == 0x3F);
}

TEST_CASE("internal-only flags do not leak into the §6.2 flags byte") {
    // kWeirdValue / kWifiUp / kConfigChanged have no §6.2 bit; only validity remains.
    const EventFlags ev = cfg::flag::kWeirdValue | cfg::flag::kWifiUp | cfg::flag::kConfigChanged;
    const Payload    p  = ble_payload::encode(2000, 2000, ev, 0);
    CHECK(p[7] == (ble_payload::bit::kInnerValid | ble_payload::bit::kOuterValid)); // 0x30
}

TEST_CASE("seq passes through unchanged across the byte range") {
    // The module is a pure passthrough for seq; wrap arithmetic lives in app/ble_task.
    CHECK(ble_payload::encode(0, 0, 0, 0)[8] == 0);
    CHECK(ble_payload::encode(0, 0, 0, 128)[8] == 128);
    CHECK(ble_payload::encode(0, 0, 0, 255)[8] == 255);
}

TEST_CASE("zero temperatures encode as zero bytes and stay valid") {
    const Payload p = ble_payload::encode(0, 0, 0, 7);
    CHECK(p[3] == 0x00);
    CHECK(p[4] == 0x00);
    CHECK(p[5] == 0x00);
    CHECK(p[6] == 0x00);
    CHECK((p[7] & ble_payload::bit::kInnerValid) != 0U); // 0 °C is a valid reading
    CHECK((p[7] & ble_payload::bit::kOuterValid) != 0U);
}
