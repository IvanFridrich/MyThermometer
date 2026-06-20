#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdint>

#include "Config.h"
#include "beep_engine.h"

// Phase 3/5 domain test: non-blocking buzzer pattern engine, driven against a
// fake clock (explicit nowMs). Covers one-shot event tones, the alternating fire
// pattern + its expiry, fire/event priority, the global on/off, stop(), and the
// millis() wrap.

namespace {
using beep::BeepEngine;
constexpr uint16_t kEventMs = cfg::beep::kBeepDurationMs; // 120
constexpr uint16_t kLow     = cfg::beep::kFireToneLowHz;  // 784
constexpr uint16_t kHigh    = cfg::beep::kFireToneHighHz; // 1046
constexpr uint16_t kStep    = cfg::beep::kFireToneStepMs; // 300
constexpr uint32_t kBurst   = cfg::beep::kFireBurstMs;    // 60000
constexpr uint16_t kTestHz  = cfg::beep::kTestToneHz;     // 1000
} // namespace

TEST_CASE("an idle engine is silent and inactive") {
    BeepEngine e;
    CHECK(e.tick(1000) == 0);
    CHECK_FALSE(e.isActive());
}

TEST_CASE("a one-shot tone plays for its duration then goes silent") {
    BeepEngine e;
    e.playTone(kTestHz, 1000);
    CHECK(e.isActive());
    CHECK(e.tick(1000) == kTestHz);
    CHECK(e.tick(1000 + kEventMs - 1) == kTestHz);
    CHECK(e.tick(1000 + kEventMs) == 0); // expired
    CHECK_FALSE(e.isActive());
}

TEST_CASE("a disabled engine ignores triggers and stays silent") {
    BeepEngine e;
    e.setEnabled(false);
    e.playTone(kTestHz, 1000);
    CHECK_FALSE(e.isActive());
    CHECK(e.tick(1000) == 0);
    e.playFire(1000);
    CHECK(e.tick(1000) == 0);
}

TEST_CASE("disabling mid-tone silences output") {
    BeepEngine e;
    e.playTone(kTestHz, 1000);
    CHECK(e.tick(1000) == kTestHz);
    e.setEnabled(false);
    CHECK(e.tick(1050) == 0);
}

TEST_CASE("the fire pattern alternates low and high each step") {
    BeepEngine e;
    e.playFire(1000);
    CHECK(e.tick(1000) == kLow); // step 0
    CHECK(e.tick(1000 + kStep - 1) == kLow);
    CHECK(e.tick(1000 + kStep) == kHigh); // step 1
    CHECK(e.tick(1000 + 2 * kStep - 1) == kHigh);
    CHECK(e.tick(1000 + 2 * kStep) == kLow); // wraps back to step 0
    CHECK(e.tick(1000 + 3 * kStep) == kHigh);
}

TEST_CASE("the fire pattern expires after the burst window") {
    BeepEngine e;
    e.playFire(1000);
    CHECK(e.tick(1000 + kBurst - 1) != 0); // still sounding just before the end
    CHECK(e.tick(1000 + kBurst) == 0);     // expired
    CHECK_FALSE(e.isActive());
    // Natural expiry releases fire ownership, so a one-shot can play again.
    e.playTone(kTestHz, 1000 + kBurst + 10);
    CHECK(e.tick(1000 + kBurst + 10) == kTestHz);
}

TEST_CASE("fire has priority: a one-shot cannot interrupt it") {
    BeepEngine e;
    e.playFire(1000);
    e.playTone(kTestHz, 1100); // ignored while fire plays
    CHECK(e.tick(1100) == kLow);
}

TEST_CASE("fire overrides an in-progress one-shot") {
    BeepEngine e;
    e.playTone(kTestHz, 1000);
    CHECK(e.tick(1000) == kTestHz);
    e.playFire(1050); // takes over immediately
    CHECK(e.tick(1050) == kLow);
}

TEST_CASE("stop silences and deactivates") {
    BeepEngine e;
    e.playFire(1000);
    e.stop();
    CHECK_FALSE(e.isActive());
    CHECK(e.tick(1100) == 0);
    // After stop, a one-shot can play again (fire no longer owns the buzzer).
    e.playTone(kTestHz, 1200);
    CHECK(e.tick(1200) == kTestHz);
}

TEST_CASE("timing is correct across a millis() wrap") {
    BeepEngine     e;
    const uint32_t t0 = 0xFFFFFFF0U;
    e.playTone(kTestHz, t0);
    CHECK(e.tick(t0) == kTestHz);
    CHECK(e.tick(t0 + kEventMs - 1) == kTestHz); // wraps; modular elapsed < duration
    CHECK(e.tick(t0 + kEventMs) == 0);           // expired
}
