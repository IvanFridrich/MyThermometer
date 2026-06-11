// =============================================================================
//  Config.h  --  SINGLE SOURCE OF TRUTH for the ESP32-S3 Thermometer firmware
// -----------------------------------------------------------------------------
//  RULE (enforced by code-review): NO magic constants anywhere else in the
//  codebase. Every tunable value, pin, threshold, timing and capacity lives
//  here. Modules #include "Config.h" and read cfg::* — they never hard-code.
//
//  Style: compile-time constants in a namespace (type-safe, no preprocessor
//  surprises). Pins are constexpr too. Secrets (WiFi / SMTP credentials) do
//  NOT live here — they live in secrets.h (git-ignored). See secrets.h.example.
//
//  Target: ESP32-S3 (dual core, NO PSRAM), Arduino framework / PlatformIO.
// =============================================================================
#pragma once

#include <cstdint>

namespace cfg {

// ----------------------------------------------------------------------------
// 1. PIN MAP  (FILL THESE IN for your wiring — placeholders below)
//    Two DS18B20 each on its OWN OneWire bus (robustness + easy identification).
//    LCD HD44780 in 4-bit mode (Arduino LiquidCrystal).
//    Passive buzzer + LCD contrast both driven by LEDC PWM.
// ----------------------------------------------------------------------------
namespace pin {
// --- DS18B20 (separate buses) ---
constexpr uint8_t kOneWireInside  = 4; // inside sensor bus (fire detection)
constexpr uint8_t kOneWireOutside = 5; // outside sensor bus

// --- HD44780 2x8 LCD, 4-bit ---
constexpr uint8_t kLcdRs = 15;
constexpr uint8_t kLcdEn = 16;
constexpr uint8_t kLcdD4 = 17;
constexpr uint8_t kLcdD5 = 18;
constexpr uint8_t kLcdD6 = 8;
constexpr uint8_t kLcdD7 = 6; // GPIO 3 is a strapping pin on ESP32-S3 — use 6

// --- PWM outputs ---
constexpr uint8_t kBuzzer        = 9;  // passive buzzer (tone generation)
constexpr uint8_t kLcdContrastV0 = 10; // PWM -> R + C low-pass -> LCD V0
constexpr uint8_t kLcdBacklight  = 11; // optional; set kBacklightControlled
} // namespace pin

// ----------------------------------------------------------------------------
// 2. LEDC (PWM) CHANNELS / TIMERS
//    Buzzer needs a variable-frequency channel (tones); contrast a fixed-freq
//    high-resolution channel (smooth DC after RC filter).
// ----------------------------------------------------------------------------
namespace ledc {
constexpr uint8_t kBuzzerChannel    = 0;
constexpr uint8_t kContrastChannel  = 1;
constexpr uint8_t kBacklightChannel = 2;

constexpr uint32_t kContrastFreqHz  = 25000; // above audible; RC-smoothed
constexpr uint8_t  kContrastResBits = 8;     // 0..255 duty (NVS uint8)
constexpr uint16_t kContrastDefault = 128;   // startup duty (tune on HW)

constexpr uint8_t kBuzzerResBits = 10;
} // namespace ledc

// Set false if backlight is hard-wired ON (then kLcdBacklight is unused).
constexpr bool kBacklightControlled = false;

// ----------------------------------------------------------------------------
// 3. TEMPERATURE MEASUREMENT
// ----------------------------------------------------------------------------
namespace temp {
constexpr uint8_t kResolutionBits = 12; // DS18B20 12-bit, 0.0625 C

// "Weird value" plausibility window. Outside => WEIRD_VALUE flag.
constexpr float kValidMinC = -30.0F;
constexpr float kValidMaxC = 80.0F;

// Fire detection: INSIDE sensor only, evaluated on the CURRENT reading.
constexpr float kFireThresholdC = 45.0F;
constexpr float kFireClearC     = 43.0F; // hysteresis (clear lower)

// Difference alarm: computed from the 10-min AVERAGES, diff = outside-inside.
// Drives the window recommendation. Two directions are flagged separately.
constexpr float kDiffThresholdC = 2.0F; // |avg_out - avg_in| trips
constexpr float kDiffClearC     = 1.5F; // hysteresis (clear lower)

// DS18B20 power-on/disconnect sentinel (reads ~85.0 C when not converted /
// line floating). Treated together with CRC failure as SENSOR_OPEN.
constexpr float kDisconnectSentinelC = 85.0F;
} // namespace temp

// ----------------------------------------------------------------------------
// 4. SAMPLING, AVERAGING, HISTORY (all STATIC, lives in RAM, lost on power off)
// ----------------------------------------------------------------------------
namespace sample {
constexpr uint32_t kSamplePeriodMs   = 60UL * 1000UL;     // measure 1x / minute
constexpr uint8_t  kAvgWindowMinutes = 10;                // floating average (= history stride)
constexpr uint8_t  kAvgWindowSamples = kAvgWindowMinutes; // 1 sample/min

constexpr uint32_t kHistoryStrideMs = 10UL * 60UL * 1000UL; // store 1/10 min
constexpr uint16_t kHistoryHours    = 24;                   // tune freely
// Ring-buffer depth. Static array of this many records.
constexpr uint16_t kHistoryDepth =
    static_cast<uint16_t>((kHistoryHours * 60U) / 10U); // = 144 @ 24h

// Stored temperature scaling: centi-degrees in int16 (range -30..80 C ok).
constexpr float kStoreScale = 100.0F; // C * 100
} // namespace sample

// ----------------------------------------------------------------------------
// 5. EVENT FLAGS (bitfield stored per history record AND used as live state)
// ----------------------------------------------------------------------------
namespace flag {
constexpr uint16_t kBoot         = 1U << 0;
constexpr uint16_t kFire         = 1U << 1; // inside >= fire thr
constexpr uint16_t kSensorOpen   = 1U << 2; // disconnected / no device
constexpr uint16_t kOneWireErr   = 1U << 3; // CRC / bus error
constexpr uint16_t kWeirdValue   = 1U << 4; // outside plausibility
constexpr uint16_t kDiffExceeded = 1U << 5; // |avg_out - avg_in| >= threshold
constexpr uint16_t kWifiDown     = 1U << 7; // wifi lost during interval
constexpr uint16_t kEmailSent    = 1U << 8; // alarm email dispatched
constexpr uint16_t kEmailFailed  = 1U << 9; // alarm email failed
} // namespace flag

// ----------------------------------------------------------------------------
// 6. WINDOW RECOMMENDATION (the actual use case of the difference alarm)
//    Goal selects what "good" means. Persisted in NVS, settable from web.
// ----------------------------------------------------------------------------
namespace window_advisor {
enum class Goal : uint8_t { kCoolRoom = 0, kWarmRoom = 1 };
constexpr Goal kDefaultGoal = Goal::kCoolRoom;
// With kCoolRoom: outside cooler than inside by > kDiffThresholdC => "OPEN".
// With kWarmRoom: outside warmer than inside by > kDiffThresholdC => "OPEN".
} // namespace window_advisor

// ----------------------------------------------------------------------------
// 7. BEEPER / TONES (passive buzzer, multi-frequency, non-blocking patterns)
//    One-shot beeps per event use distinct frequencies. Fire alarm repeats a
//    perfect-fourth (4:3) interval for ~1 minute.
// ----------------------------------------------------------------------------
namespace beep {
constexpr uint16_t kDiffToneHz     = 1200; // difference-exceeded beep
constexpr uint16_t kSensorToneHz   = 500;  // sensor-open / onewire err
constexpr uint16_t kWeirdToneHz    = 700;  // weird value
constexpr uint16_t kBootToneHz     = 880;  // boot chirp
constexpr uint16_t kTestToneHz     = 1000; // web "test beeper"
constexpr uint16_t kBeepDurationMs = 120;  // one-shot duration

// Fire: alternate two tones a perfect fourth apart (ratio 4:3).
constexpr uint16_t kFireToneLowHz  = 784;           // G5
constexpr uint16_t kFireToneHighHz = 1046;          // ~ G5 * 4/3 (C6), a fourth
constexpr uint16_t kFireToneStepMs = 300;           // per-tone duration
constexpr uint32_t kFireBurstMs    = 60UL * 1000UL; // pattern length ~1 min
} // namespace beep

// ----------------------------------------------------------------------------
// 8. BLE  (non-connectable advertising beacon; manufacturer-specific data)
// ----------------------------------------------------------------------------
namespace ble {
constexpr uint16_t kCompanyId        = 0xFFFF; // 0xFFFF = test/unassigned
constexpr uint8_t  kPayloadVersion   = 1;
constexpr uint8_t  kBurstsPerMinute  = 5;   // advertise 5x each minute
constexpr uint32_t kBurstSpacingMs   = 150; // gap between bursts
constexpr uint16_t kAdvIntervalMinMs = 100; // within a burst
constexpr uint16_t kAdvIntervalMaxMs = 100;
constexpr char     kDeviceName[]     = "ESP32S3-Thermo";
} // namespace ble

// ----------------------------------------------------------------------------
// 9. WIFI / WEB / mDNS  (credentials are in secrets.h, NOT here)
// ----------------------------------------------------------------------------
namespace net {
constexpr char     kMdnsHostname[] = "teplomer"; // -> http://teplomer.local
constexpr uint16_t kHttpPort       = 80;         // plain HTTP (LAN-trusted)
constexpr uint32_t kReconnectMinMs = 1000;       // backoff start
constexpr uint32_t kReconnectMaxMs = 30000;      // backoff cap
constexpr uint32_t kWifiCheckMs    = 2000;       // link supervision period
} // namespace net

// ----------------------------------------------------------------------------
// 10. EMAIL ALARMS  (SMTP creds in secrets.h)
//     Policy: at most one automatic email per kMinIntervalMs; while an alarm
//     stays active, NO further automatic emails are sent. Same rule for fire
//     and sensor-open. Manual test / status email from web ignores the limiter.
// ----------------------------------------------------------------------------
namespace email {
constexpr uint32_t kMinIntervalMs = 60UL * 60UL * 1000UL; // 1 hour
constexpr uint16_t kSmtpPort      = 465;                  // SSL (Gmail app password)
constexpr uint32_t kSendTimeoutMs = 15000;
} // namespace email

// ----------------------------------------------------------------------------
// 11. DISPLAY (2x8 chars; default shows temperatures; IP/host shown briefly)
// ----------------------------------------------------------------------------
namespace lcd {
constexpr uint8_t kCols = 8;
constexpr uint8_t kRows = 2;
// On WiFi (re)connect, show hostname/IP for this long, then back to temps.
constexpr uint32_t kShowAddressMs = 60UL * 1000UL; // ~1 minute
constexpr uint32_t kRefreshMs     = 1000;          // redraw cadence
constexpr uint8_t  kDegreeGlyph   = 0;             // custom char slot for °
} // namespace lcd

// ----------------------------------------------------------------------------
// 12. WATCHDOG / SAFETY
// ----------------------------------------------------------------------------
namespace safety {
constexpr uint32_t kWdtTimeoutMs = 8000; // task WDT timeout (NFR-02)
constexpr bool     kPanicOnWdt   = true; // reset on WDT timeout
// Brownout: ESP32-S3 brownout detector enabled with a conservative level.
// (Exact register level set in HAL; value here documents intent.)
constexpr uint8_t kBrownoutLevel = 7; // see HAL brownout setup
} // namespace safety

// ----------------------------------------------------------------------------
// 13. TASKS / CORES  (ESP32-S3 dual core)
//     Core 0: WiFi + BLE stacks and their service tasks.
//     Core 1: sensing, averaging, history, alarms, display, beeper, logging.
// ----------------------------------------------------------------------------
namespace task {
constexpr int kCoreNet = 0; // wifi + ble
constexpr int kCoreApp = 1; // everything else

constexpr uint32_t kStackSensor  = 4096;
constexpr uint32_t kStackDisplay = 3072;
constexpr uint32_t kStackWeb     = 8192;
constexpr uint32_t kStackBle     = 4096;
constexpr uint32_t kStackBeeper  = 2048;

constexpr uint8_t kPrioSensor  = 5;
constexpr uint8_t kPrioDisplay = 3;
constexpr uint8_t kPrioWeb     = 4;
constexpr uint8_t kPrioBle     = 4;
constexpr uint8_t kPrioBeeper  = 6; // timing-sensitive
} // namespace task

// ----------------------------------------------------------------------------
// 14. LOGGING (UART over USB)
// ----------------------------------------------------------------------------
namespace log {
constexpr uint32_t kBaud = 115200;
enum class Level : uint8_t { kTrace = 0, kDebug, kInfo, kWarn, kError };
constexpr Level kMinLevel = Level::kDebug; // verbose by default
} // namespace log

} // namespace cfg
