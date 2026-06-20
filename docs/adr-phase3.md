# ADR Phase 3 — HAL target drivers (sensors, LCD, PWM)

Decisions made during Phase 3 implementation of the ESP32-S3 target drivers
(`onewire_bus_target.cpp`, `display_target.cpp`, `pwm_target.cpp`). Each ADR records
the choice, the rejected alternatives, and the binding constraint.

Verified state: `pio run -e esp32-s3-devkitc-1` links (RAM 5.5 %, Flash 17.8 %).
On-hardware smoke test (two sensors, LCD, contrast, buzzer, sensor-detach) is the
owner's manual acceptance step — it cannot be run in CI.

---

## ADR-10: OneWire via the bit-banged library (NFR-08 RMT deviation)

**Decision:** The DS18B20 driver uses the bit-banged `paulstoffregen/OneWire`
library, not the RMT peripheral. Owner-approved deviation from NFR-08 / §8.4 /
the `onewire-ds18b20-rmt` skill, which mandate RMT timing.

**Rejected:** (a) Custom RMT TX/RX OneWire driver — jitter-free and offloads the
CPU per NFR-08, but ~250-350 lines of peripheral code that cannot be validated
without hardware and would remove `paulstoffregen/OneWire` from `lib_deps`.
(b) ESP-IDF `onewire_bus` managed component — awkward to wire into a PlatformIO
Arduino build.

**Binding constraint / mitigation:** The library masks interrupts **per bit**
(~70 µs read/write slot), not per transaction — so the longest contiguous
interrupt-off window is a single bit slot, not a whole 9-byte scratchpad read.
This is isolated to **Core 1** (`cfg::task::kCoreApp`), where the measurement
task runs, away from the WiFi/BLE stacks pinned to Core 0 (NFR-07). A single bus
transaction is sub-millisecond of bus time and runs once per 60 s sample, far
under the 8 s task WDT (NFR-02).

**Revisit if:** OneWire glitches appear under load, or sub-second sampling is
added — then move to the custom RMT driver (the HAL interface is unchanged, only
`onewire_bus_target.cpp` and the target-private members would change).

---

## ADR-11: Per-instance target storage via `#ifndef NATIVE_BUILD` members

**Decision:** Target-only per-instance state lives in HAL-header members guarded
by `#ifndef NATIVE_BUILD` (the mirror of the existing fake-only `#ifdef
NATIVE_BUILD` blocks, ADR-03). `OneWireBus` holds its `OneWire ow_`, `pin_` and
`convStarted_`; `Display` holds its `LiquidCrystal lcd_`; `Pwm` holds its two LEDC
channel numbers. The matching Arduino headers are included under the same guard.

**Rejected:** (a) File-scope statics in the `.cpp` (the original stub pattern) —
**broken** for `OneWireBus`, which is instantiated twice (`innerBus()` and
`outerBus()`): a single static would alias both buses. (b) A `this`-keyed static
registry — broken by the by-value `wiring::*` factories, which move the object so
`this` is not stable. (c) Pimpl — needs heap (NFR-04).

**Binding constraint:** Two independent OneWire buses (D3), by-value wiring
factories, zero dynamic allocation (NFR-04), and the single-header link-time seam
(ADR-01/03). Members travel with the (copyable) object; public API is unchanged.

---

## ADR-12: Non-blocking DS18B20 conversion (read-previous / trigger-next)

**Decision:** `readCentiC()` never blocks for the ~750 ms 12-bit conversion.
Each call reads the scratchpad produced by the conversion started on the
*previous* call, then starts the next conversion. The first call after
construction returns `kNotReady`. CRC is validated (`kOneWireErr` on failure);
absence of a presence pulse returns `kSensorOpen`; an all-zero scratchpad (which
passes the trivial CRC(0)=0) is rejected as `kOneWireErr`.

**Rejected:** Blocking convert-wait-read (~750 ms × 2 sensors per minute) — simpler
but stalls Core 1 work (LCD, alarms) and contradicts the skill's "don't block the
whole second" guidance.

**Binding constraint:** 60 s sample cadence guarantees the conversion is complete
by the next call. The `WEIRD_VALUE`/sentinel classification stays in the domain
`AnomalyDetector` (the driver returns raw centi-°C and protocol errors only),
preserving the HAL/domain split (NFR-10).

---

## ADR-13: LEDC channel API (arduino-esp32 2.0.17) + LiquidCrystal dependency

**Decision:** PWM uses the channel-based LEDC API of the installed framework
(`ledcSetup`/`ledcAttachPin`/`ledcWrite`/`ledcWriteTone`), matching the HAL's
`initContrast(pin, channel)` / `initBuzzer(pin, channel)` signatures. Contrast is
a fixed-frequency 8-bit channel (`cfg::ledc::kContrastFreqHz`,
`kContrastResBits`); the buzzer is a variable-frequency channel driven by
`ledcWriteTone`, silenced with duty 0. `arduino-libraries/LiquidCrystal` was added
to `lib_deps` (it is not bundled with the ESP32 framework).

**Rejected:** The arduino-esp32 3.x pin-based LEDC API (`ledcAttach`,
`ledcWrite(pin, …)`) — not available on the pinned official `espressif32` platform
(framework 2.0.17), and it would not match the channel-carrying HAL interface.

**Binding constraint:** The non-blocking beep/fire pattern engine is **app-layer**
(Phase 5); these drivers expose only the LEDC primitives (`tone`, `noTone`,
`setContrastDuty`). The buzzer is passive, so pitch = LEDC frequency (D7).
