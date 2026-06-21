# Traceability Matrix — ESP32 Teplomer

Maps every FR and NFR from SPECIFICATION.md §2-3 to the module(s) that implement it
and the test method used to verify it.

Phase column: earliest phase where the requirement is satisfied.

Last updated: Phase 8 (2026-06-21). Software-verifiable items confirmed; items marked
"HW test" require on-device acceptance testing (SPECIFICATION.md §10 A1-A9).

## Functional Requirements

| ID    | Requirement (short) | Module(s) | Test method | Phase |
|-------|---------------------|-----------|-------------|-------|
| FR-01 | Measure both sensors 1×/min | `src/main.cpp` core1Task, `hal/onewire_bus_target` | Driver impl (Phase 3); 60 s cadence wired in `main.cpp` (kSamplePeriodMs); HW smoke test pending | 3/4 |
| FR-02 | 10-min moving average per sensor | `core/moving_average` | `test/test_moving_average` (warm-up, wrap, invalid exclusion) | 2 |
| FR-03 | Detect weird values (-30..80 °C), flag WEIRD_VALUE | `core/anomaly` | `test/test_anomaly` (bounds, sentinel, kTempInvalid) | 2 |
| FR-04 | Detect OneWire CRC/open, flag after 3 consecutive errors | `core/anomaly`, `hal/onewire_bus_target` | Driver returns kSensorOpen (no presence) / kOneWireErr (CRC, all-zero); 85 °C & −127 °C sentinel classification is in `core/anomaly` (ADR-12), `test/test_anomaly`; threshold/recovery `test/test_anomaly` + HW | 2 |
| FR-05 | Ring buffer 144 records, static RAM | `core/history_buffer` | `test/test_history_buffer` + `static_assert sizeof==6` | 2 |
| FR-06 | One record = two avg temps + OR flags per 10-min window | `core/history_buffer`, `app/measurement_task` | `test/test_history_buffer` (flags verbatim) + integration | 2 |
| FR-07 | Diff alarm: \|avg_out−avg_in\| ≥ 2 °C → beep+flag, clear 1.5 °C | `core/alarm_state`; window recommendation `core/window_advice` (D9) | `test/test_alarm_state` (diff hysteresis, both goals); `test/test_window_advice` (OPEN/CLOSE/NO_CHANGE truth table, 100% cov) | 2 |
| FR-08 | Fire alarm: instant inner ≥ 45 °C → repeat alarm, clear 43 °C | `core/alarm_state`; audio `core/beep_engine` (perfect-fourth fire pattern; re-armed while FIRE persists by the buzzer task, Phase 5) | `test/test_alarm_state` (fire hysteresis, no flap) + `test/test_beep_engine` (pattern timing/expiry, fake clock) | 2 |
| FR-09 | Sensor fault → distinctive beep + flag + email | `core/alarm_state`, `core/email_policy`, `app/mail_task` | `test/test_alarm_state` (sensor edges) + `test/test_email_policy` (§7 send decision) + integration | 2/5 |
| FR-10 | Fire detection only on inner sensor | `core/alarm_state` | `test/test_alarm_state` (innerRaw drives fire only) | 2 |
| FR-11 | LCD row 1: temps; row 2: rotating pages (IP, uptime, status) | `src/main.cpp` renderLcd, `hal/display_target` | Driver impl (Phase 3); row0 I/O alternation + row1 priority status (FIRE/SENSOR/WiFi DN/uptime). Full ~3 s/page rotation simplified to priority — refine at HW bring-up; HW smoke test | 3/4 |
| FR-12 | Show IP on boot | `src/main.cpp` (logs IP) | IP logged to UART on connect. LCD IP display needs scrolling on the 2×8 panel (>8 chars) — deferred to HW bring-up; HW smoke test | 4 |
| FR-13 | Contrast via LEDC PWM | `hal/pwm_target` | Driver impl (Phase 3, LEDC ch1, 8-bit); HW smoke test pending | 3 |
| FR-14 | Connect to hardcoded SSID from secrets.h | `hal/wifi_hal_target` | Driver impl (Phase 4, WiFi STA); HW integration test | 4 |
| FR-15 | Reconnect + exponential backoff + LCD "WiFi DN" | `hal/wifi_hal_target`, `src/main.cpp` core0Task | Backoff state machine (1s→30s cap) + "WiFi DN" LCD wired in `main.cpp`; integration (disconnect AP) | 4 |
| FR-16 | mDNS `teplomer.local` | `hal/wifi_hal_target` | Driver impl (Phase 4, ESPmDNS + http service); browser access test | 4 |
| FR-17 | Single-page web UI with history graph | `web/index.html` (Pico.css + uPlot CDN), `app/web_task` | Page implemented (live panel, window advice, config, actions); browser test on device | 7 |
| FR-18 | uPlot graph with missing-point rendering | `web/app.js` | Implemented: null samples render as gaps (`spanGaps:false`), browser computes time from uptime_s+stride_s; browser visual test | 7 |
| FR-19 | Web shows: uptime, errors, free heap, RSSI, sensor IDs | `web/app.js`, `core/json_api` (`/api/current` fields) | `test/test_json_api` (exact shape/values); browser test | 7 |
| FR-20 | Config via web, persisted in NVS | `hal/nvs_store_target`, `app/web_task` | Driver impl (Phase 4, Preferences); NVS round-trip + browser | 4 |
| FR-21 | Web actions: restart, test beep, set contrast, test email, status email | `src/main.cpp` handleAction | All 5 actions wired (`POST /api/action/{}`); manual email bypasses §7 limiter; browser integration test | 4/5 |
| FR-22 | Web without authentication (LAN-trusted) | `hal/http_server_target` | Driver impl (Phase 4, plain HTTP no auth); code review + design doc | 4 |
| FR-23 | JSON API endpoints | `core/json_api`, `src/main.cpp` (WebServer handlers) | `core/json_api` serializers `test/test_json_api` (exact-string, null-on-invalid, bounded, 88% cov); `/`, `/app.js`, `/api/current`, `/api/history`, `/api/config` handlers wired in `main.cpp` (Arduino WebServer; HttpServer HAL too thin for bodies); browser/curl test | 4 |
| FR-24 | BLE beacon 5 bursts/min, manufacturer data §6.2 | `hal/ble_advertiser_target`, `core/ble_payload`, `app/ble_task` | Driver impl (Phase 4, NimBLE non-conn); §6.2 encoder `core/ble_payload` with byte-exact `test/test_ble_payload` (100% cov); burst cadence in `app/ble_task`; Python bleak monitor | 4 |
| FR-25 | UART log 115200, structured format | `core/event_log`, `src/main.cpp` logLine | `[+uptime][level][module] msg` to Serial @115200 + in-RAM EventLog, wired in `main.cpp`; terminal observation | 5 |
| FR-26 | Email on fire + sensor fault, rate-limited 1×/h | `core/email_policy`, `src/main.cpp` checkEmail, `hal/mailer_target` | `test/test_email_policy` (§7, 100% cov; rate-limit anchors on *success* via shouldSend/markSent so a failed send retries). main.cpp gates on WiFi, sets EMAIL_SENT/EMAIL_FAILED window flags, WDT-unsubscribed + kSendTimeoutMs-bounded send; HW integration | 5 |
| FR-27 | All config fields persistent in NVS, default on empty | `hal/nvs_store_target`, `core/config_model` | Driver impl (Phase 4, Preferences typed get/put); `test/test_config_model` + NvsStoreFake round-trip | 4 |
| FR-28 | Python bleak monitor decodes advertising packets | `tools/ble_monitor/monitor.py` | `--selftest` decodes §6.2 vectors (byte-matched to firmware encoder); manual Windows scan | 7 |

## Non-Functional Requirements

| ID     | Requirement (short) | Module(s) / Mechanism | Verified by | Phase |
|--------|---------------------|-----------------------|-------------|-------|
| NFR-01 | Survives WiFi outage, sensor detach, voltage dip | `app/supervisor`, reconnect logic; WiFi backoff (1 s→30 s) in core0Task; sensor anomaly → kTempInvalid path | Fault injection: WiFi disconnect tested; sensor detach → kSensorOpen after N=3 consec errors; voltage dip = BOD HW test | 6 |
| NFR-02 | Task WDT, 8 s timeout, reset on hang | `src/main.cpp` setup(): `esp_task_wdt_deinit()` + `esp_task_wdt_init(8s, panic=true)`; both pinned tasks `esp_task_wdt_add(NULL)` + `esp_task_wdt_reset()` per cycle | Fix: deinit-before-init guarantees our timeout (Arduino pre-inits TWDT before setup()); artificial-deadlock HW test pending | 6 |
| NFR-03 | Brownout detector enabled, clean reset | ESP-IDF sdkconfig `CONFIG_ESP_BROWNOUT_DET=y`; `hal/system_hal_target` resetReason() maps ESP_RST_BROWNOUT → `cfg::flag::kBrownoutRecover` | Boot log "BROWNOUT_RECOVER" observable; voltage-ramp HW test pending | 6 |
| NFR-04 | No heap alloc in steady state; static stacks/buffers | `xTaskCreateStaticPinnedToCore` + file-scope `StaticTask_t` + `StackType_t` arrays; ETL containers; `g_emailBody[512]` static; `g_jsonBuf` static | `min_free_heap` surfaced in /api/current; ASan/UBSan clean on host; 24 h soak test (HW, pending) | 6 |
| NFR-05 | `-fno-exceptions -fno-rtti`; errors via `Result<T>` | `include/result.h` + all `.cpp`; platformio.ini build flags | Build flags verified; `pio run -e esp32-s3-devkitc-1` clean; code review | 0 |
| NFR-06 | No runtime virtual dispatch; link-time seam | HAL headers (no vtable), `build_src_filter` in platformio.ini | Static analysis; `grep -r virtual src/` clean; code review | 1 |
| NFR-07 | Core 0: WiFi+BLE+web+mail; Core 1: sensing+LCD+history | `src/main.cpp`: `xTaskCreateStaticPinnedToCore` with `cfg::task::kCoreNet`/`kCoreApp`; mutex-guarded snapshot | Both static-stack tasks pinned correctly; core-affinity visible in UART log; review OK | 6 |
| NFR-08 | OneWire via RMT; PWM via LEDC | `hal/onewire_bus_target`, `hal/pwm_target` (LEDC ch 0 buzzer, ch 4 contrast — independent timers) | **DEVIATION**: OneWire bit-banged not RMT (owner-approved, ADR-10); LEDC channel separation fix: kContrastChannel=4 (Timer 2) isolated from kBuzzerChannel=0 (Timer 0) to prevent ledcWriteTone() corruption | 3 |
| NFR-09 | Stack protection; no secrets in git | `-fstack-protector-strong` via ESP-IDF defaults; `.gitignore` covers secrets.h; CI pre-commit secret scan | Code review + CI green; `git log --all -- include/secrets.h` returns empty | 0 |
| NFR-10 | All HW behind HAL; domain testable on host | HAL link-time seam (13 test suites all pass without HW) | `pio test -e native` → 13 suites PASSED | 1 |
| NFR-11 | clang-format/tidy clean; ASan/UBSan; coverage ≥ 85 % domain | CI: build-firmware + native-tests (clang++-18 + ASan/UBSan) + static-analysis + sonar | CI green; clang-format --dry-run -Werror clean on all src/include/test files (Phase 8 verified) | all |
| NFR-12 | No god objects; narrow module interfaces | 8 core/ modules + 9 HAL modules, each with single responsibility | Architect review (Phase 1 ADR); code review each phase | 1 |

---

## Phase 8 — Acceptance Checklist

Status legend: ✅ done  ⏳ requires on-device HW test  ❌ not done

| # | Item | Status |
|---|------|--------|
| A1 | inner ≥ 45 °C → fire pattern + FIRE flag + email (≤1/h) | ⏳ HW |
| A2 | \|avg_out−avg_in\| ≥ 2 °C → double beep; clear at 1,5 °C | ⏳ HW |
| A3 | Sensor detach → SENSOR_OPEN ≤ 3 min, email, pattern; recovery after reconnect | ⏳ HW |
| A4 | Restart → history cleared (OK), NVS config survives | ⏳ HW |
| A5 | AP disconnect → WiFi DN on LCD, reconnect; web available again without restart | ⏳ HW |
| A6 | Web graph shows gap where sample is missing/INVALID | ✅ code review (spanGaps:false, null mapping) |
| A7 | BLE packet decoded by Python monitor with correct temps + flags | ✅ --selftest passes; `test/test_ble_payload` 100% cov |
| A8 | Artificially hung task → WDT reset within 8 s | ⏳ HW |
| A9 | 24 h soak → min_free_heap stable (no leak) | ⏳ HW |

Software-verifiable DoD items (no HW needed):

| Item | Status |
|------|--------|
| All 13 native test suites green | ✅ `pio test -e native` |
| clang-format clean (all src/include/test) | ✅ `clang-format --dry-run -Werror` |
| No secrets in git | ✅ `git log --all -- include/secrets.h` empty |
| secrets.h.example current (all 6 macros) | ✅ |
| Traceability matrix complete (FR-01…FR-28, NFR-01…NFR-12) | ✅ this document |
| README runbook (wiring, flash, web, BLE monitor, UART) | ✅ |
| ASan/UBSan clean (via scripts/coverage.ps1) | ✅ CI green |
| Domain coverage ≥ 85 % | ✅ CI llvm-cov report |
