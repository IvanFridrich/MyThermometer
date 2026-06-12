# ADR Phase 1 — Architecture & HAL seam decisions

Decisions made or confirmed during Phase 1 implementation.
Each ADR records the choice, the rejected alternatives, and the binding constraint.

---

## ADR-01: HAL link-time seam (no virtual dispatch)

**Decision:** HAL classes are concrete classes with non-virtual methods. Two implementations are provided for each unit: `*_target.cpp` (ESP32 drivers) and `*_fake.cpp` (host test doubles). PlatformIO's `build_src_filter` selects which implementation is linked:
- `esp32-s3-devkitc-1` env: excludes `hal/*_fake.cpp`
- `native` env: excludes all non-fake `hal/` files

**Rejected:** Runtime virtual dispatch (`abstract base class` + `unique_ptr<IDisplay>`). This would require RTTI, heap allocation (or static vtable + placement tricks), and violates NFR-05/06.

**Binding constraint:** `-fno-exceptions -fno-rtti` and the "zero dynamic allocation in steady state" invariant (NFR-04/05/06).

---

## ADR-02: `Result<T>` as the universal error type

**Decision:** A lightweight discriminated union `Result<T>` / `Result<void>` is used throughout. The `Status` enum covers all error categories. There is NO `throw`, NO `std::expected` (C++23, not available here), NO errno.

**Rejected:** Returning error codes as raw integers (type-unsafe). `std::expected` requires C++23 and GCC 12+. `std::optional` loses the error category.

**Binding constraint:** `-fno-exceptions` + C++17 target (NFR-05).

---

## ADR-03: `#ifdef NATIVE_BUILD` allowed only in HAL headers

**Decision:** HAL headers (`include/hal/*.h`) and `include/clock.h` use `#ifdef NATIVE_BUILD` blocks to declare fake-only members (injectable state, inspection API) and test-only methods. Domain headers (`include/*.h` — everything outside `include/hal/`) MUST NOT contain `#ifdef NATIVE_BUILD`.

**Rejected:** Separate "fake header" / "target header" per unit — would require two header sets and conditional include paths. Pimpl (opaque pointer) — requires heap allocation.

**Binding constraint:** Single header pair per module (contract once, two link-time implementations). Keeps host tests simple: tests access `fake.setNextReading(4500)` directly on the class.

---

## ADR-04: `Temperature` = `int16_t` centi-°C; `INT16_MIN` = invalid

**Decision:** Temperatures are stored as `int16_t` in centi-degrees Celsius. The sentinel `kTempInvalid = INT16_MIN` (-32768 ≈ -327.68 °C) is used wherever a reading is unavailable or invalid. Range -30..80 °C maps to -3000..8000.

**Rejected:** `float` (4 bytes vs 2, unnecessary for 0.01 °C resolution at this range). `int32_t` (doubles storage without benefit). A separate `bool valid` field (doubles struct size, complicates comparison).

**Binding constraint:** 6-byte `HistoryRecord` (§6.1): 2 × int16_t + uint16_t flags = 6 bytes packed. Total 864 bytes for 144 records (NFR-04).

---

## ADR-05: `MovingAverage<N>` as a header-only template

**Decision:** `MovingAverage` is a class template with full inline implementation in `include/moving_average.h`. Invalid samples (`kTempInvalid`) are stored but excluded from the average computation.

**Rejected:** Non-template fixed-size class (would need `N` as a runtime parameter or a compile-time constant in the type, which leads to either runtime overhead or an awkward global constant dependency). ETL `etl::moving_average` (would create an ETL dependency in the core layer test).

**Binding constraint:** Template parameter `N` is `uint8_t` to enforce N ≤ 255 and match `cfg::sample::kAvgWindowSamples = 10`.

---

## ADR-06: `HistoryBuffer` stores 144 × 6 B in a `std::array`

**Decision:** `std::array<HistoryRecord, 144>` placed as a member of `HistoryBuffer`. The buffer is allocated as a static global (or stack in unit tests). Oldest-first indexing via `at(idx)`.

**Rejected:** ETL ring buffer (ETL is not available in native env without adding it to `lib_deps`; `std::array` + manual head/count indexing is trivially correct and readable). Heap allocation (violates NFR-04).

**Binding constraint:** 864 bytes fixed. No PSRAM on this board (D1). Must fit in internal SRAM budget.

---

## ADR-07: `AlarmState` takes `ConfigModel` by const reference

**Decision:** `AlarmState` holds a `const ConfigModel&` reference to thresholds. This means the caller must keep the `ConfigModel` instance alive for the lifetime of `AlarmState` and configuration changes take effect at the next `update()` call.

**Rejected:** `AlarmState` owning a `ConfigModel` copy (would require copy on every NVS change). Passing thresholds as individual `update()` parameters (awkward API with 6+ parameters).

**Binding constraint:** Single ownership of `ConfigModel` in the application task; `AlarmState` is a dependent subscriber.

---

## ADR-08: `NvsStore` fake uses a fixed-size flat key-value table

**Decision:** `NvsStoreFake` stores up to `kNvsMaxEntries = 16` entries in a flat array of `Entry` structs (key + up to 4 bytes of raw data). No ETL, no `std::map`, no heap.

**Rejected:** `etl::map` (would require ETL in native env `lib_deps`). `std::map` (heap allocation, inconsistent with the "no dynamic alloc" spirit even in tests).

**Binding constraint:** 16 entries covers all 8 config keys in §6.4 with headroom. The fake is not intended to simulate NVS namespace isolation — it's a simple in-memory store for unit tests.

---

## ADR-09: `Clock` uses a global static counter in the fake

**Decision:** `clock_fake.cpp` uses a single `static uint32_t g_now_ms` variable shared across all `Clock` instances in a test binary. `Clock::reset()` zeroes it; `advanceMs()` increments it.

**Rejected:** Per-instance counter (would require member state in the Clock class, but the clock target has no members — adding a fake member would need `#ifdef NATIVE_BUILD` and increase target binary size by 4 bytes unnecessarily).

**Implication:** Tests using `Clock` must call `clk.reset()` at the start if they rely on a known starting point, because all `Clock` instances share the same counter.

---

## ADR-10: `wiring.h` as factory functions, not global singletons

**Decision:** `src/app/wiring.h` provides `inline` factory functions (e.g. `wiring::display()`) that construct and return HAL objects. The actual object lifetimes are managed by the application tasks (Phase 4+).

**Rejected:** Global singleton objects in `wiring.cpp` (would make the application layer difficult to test in isolation and create order-of-initialization problems with C++ static objects). Dependency injection container (over-engineered for this firmware size).

**Binding constraint:** Wiring module must NOT be compiled in native env (only app tasks reference it, and `build_src_filter` excludes `app/` from native). Keeps native build clean.
