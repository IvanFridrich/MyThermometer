# ADR Phase 4 — Connectivity target drivers (WiFi, NVS, HTTP, BLE)

Decisions made during Phase 4 implementation of the ESP32-S3 connectivity drivers
(`wifi_hal_target.cpp`, `nvs_store_target.cpp`, `http_server_target.cpp`,
`ble_advertiser_target.cpp`). Each ADR records the choice, the rejected
alternatives, and the binding constraint.

Verified state: `pio run -e esp32-s3-devkitc-1` links (RAM 5.5 %, Flash 22.4 %);
native `test_phase1_link` passes. On-hardware acceptance (join AP, serve page,
NVS survives restart, BLE packet visible to a scanner, reconnect on AP drop) is
the owner's manual step — it needs the board and a network.

Dependency graph resolved: WiFi/WebServer/Preferences/ESPmDNS (framework 2.0.0),
NimBLE-Arduino 2.5.0, LiquidCrystal 1.0.7, OneWire 2.3.8, ETL, ESP-Mail.

---

## ADR-14: Connectivity per-instance target storage (extends ADR-11)

**Decision:** Target-only state lives in `#ifndef NATIVE_BUILD` header members, as
in Phase 3. `NvsStore` holds a `mutable Preferences prefs_` + copied namespace +
`open_`; `HttpServer` holds a `WebServer server_` constructed from the port;
`BleAdvertiser` holds a forward-declared `NimBLEAdvertising* adv_` (the full
NimBLE header is included only in the `.cpp`). `WifiHal` needs no members — the
Arduino `WiFi`/`MDNS` objects are process singletons.

**Rejected:** File-scope statics (the stub pattern) — acceptable only for true
singletons; members keep the seam uniform and copy-safe for the by-value wiring
factories. Including `<NimBLEDevice.h>` in the header — avoided via forward
declaration so includers don't pull the BLE stack.

**Binding constraint:** Single-header link-time seam (ADR-01/03), zero dynamic
allocation (NFR-04). `prefs_` is `mutable` because the HAL's `get*` accessors are
`const` but `Preferences::get*` are not — the logical (stored) state is unchanged
by a read.

---

## ADR-15: WiFi HAL is thin; reconnect/backoff is app-layer

**Decision:** `WifiHal::begin()` only *initiates* the association
(`WiFi.mode(STA)` + `WiFi.begin`) and returns `ok`; `isConnected()` reports
`WL_CONNECTED`. The infinite reconnect with exponential backoff
(`cfg::net::kReconnectMin/MaxMs`) and the `WIFI_DOWN` LCD indication (FR-15) are
orchestrated by the app/supervisor task polling `isConnected()`.

**Rejected:** A blocking `begin()` that waits for connection — would stall the
caller for seconds and complicate WDT feeding (NFR-02). Putting the backoff
state machine in the HAL — mixes policy into the driver and is not host-testable.

**Binding constraint:** NFR-10 (HW behind a thin HAL; policy in testable app/
domain code) and the non-blocking requirement.

---

## ADR-16: NVS via Preferences typed accessors

**Decision:** `NvsStore` maps its typed API onto Preferences:
bool→`getBool/putBool`, int16→`getShort/putShort`, uint8→`getUChar/putUChar`,
uint32→`getULong/putULong`. `put*` return the byte count (0 = failure →
`kStorageErr`); every accessor guards `open_` and returns `kStorageErr` when
closed, matching the fake's semantics so host tests stay representative.

**Rejected:** Raw `nvs_flash` IDF calls — more code, no benefit over Preferences.
A blob/struct dump — loses the per-key defaulting that FR-27 needs on empty NVS.

**Binding constraint:** §6.4 NVS key contract; behavioural parity with
`nvs_store_fake` so `test_config_model` + the fake round-trip cover the logic.

---

## ADR-17: HTTP via Arduino WebServer, plain, explicit method mapping

**Decision:** `HttpServer` wraps Arduino `WebServer` (project decision: WebServer,
not `esp_http_server`). Plain HTTP, no auth (D12/FR-22 — LAN-trusted, accepted
risk). The HAL's `on(uri, method, handler)` maps the int method explicitly
(`0 → HTTP_GET`, else `HTTP_POST`) rather than casting to the `HTTPMethod` enum,
whose underlying values differ (`HTTP_HEAD` is 1). The `void(*)()` handler
converts to WebServer's `std::function`.

**Rejected:** Casting `static_cast<HTTPMethod>(method)` — would silently map 1 to
`HTTP_HEAD`. esp_http_server — heavier API, not the chosen stack.

**Binding constraint:** D12 plain HTTP; the route handlers / JSON serializers are
app-layer (Phase 4/7); the app task must call `handleClient()` regularly (no
blocking, NFR-02).

---

## ADR-18: BLE non-connectable beacon, NimBLE 2.x, non-blocking burst

**Decision:** `BleAdvertiser` uses NimBLE-Arduino 2.5.0. `init()` sets the device
name, fetches the legacy `NimBLEAdvertising`, sets non-connectable
(`BLE_GAP_CONN_MODE_NON`) and the adv interval (`cfg::ble::kAdvInterval*`,
converted ms→0.625 ms units). `setPayload()` forwards the raw bytes to
`setManufacturerData(data, len)`. `burst(count, spacingMs)` calls
`adv_->start(count * spacingMs)`, which advertises for that bounded window and
auto-stops — non-blocking, no `delay()`. The §6.2 9-byte payload **encoder lives
in the domain layer** (host byte-exact test), not the HAL.

**Rejected:** Continuous advertising — the spec wants ~5 bursts/min then silence
(FR-24). A blocking start/sleep/stop loop — would stall Core 0. Building the
manufacturer data (incl. company ID) inside the HAL — the skill keeps the encoder
in testable domain code; `init()`'s `companyId` param is therefore unused (the
payload already carries it at bytes [0..1]).

**Binding constraint:** FR-24 burst cadence, NimBLE runs on Core 0 with WiFi,
bounded heap (no PSRAM), and the byte-exact §6.2 contract owned by the domain
encoder.
