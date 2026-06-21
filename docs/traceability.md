# Traceability Matrix — ESP32 Teplomer

Maps every FR and NFR from SPECIFICATION.md §2-3 to the module(s) that implement it
and the test method used to verify it.

Phase column: earliest phase where the requirement is satisfied.

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
| NFR-01 | Survives WiFi outage, sensor detach, voltage dip | `app/supervisor`, reconnect logic | Fault injection tests (Phase 6) | 6 |
| NFR-02 | Task WDT, 8 s timeout, reset on hang | `hal/system_hal_target`, `src/main.cpp` | `esp_task_wdt_init(8s, panic)` + both pinned tasks add/feed; wired in `main.cpp`; artificial-deadlock HW test (Phase 6) | 6 |
| NFR-03 | Brownout detector enabled, clean reset | ESP-IDF sdkconfig / `hal/system_hal_target` | resetReason() maps ESP_RST_BROWNOUT; boot logs recovery; BOD level via sdkconfig; voltage-ramp HW test | 6 |
| NFR-04 | No heap alloc in steady state; static stacks/buffers | All modules (ETL, static arrays) | ASan/UBSan + min_free_heap monitor | all |
| NFR-05 | `-fno-exceptions -fno-rtti`; errors via `Result<T>` | `include/result.h` + all `.cpp` | Build flags; code review | 0 |
| NFR-06 | No runtime virtual dispatch; link-time seam | HAL headers (no vtable), build_src_filter | Static analysis; code review | 1 |
| NFR-07 | Core 0: WiFi+BLE; Core 1: sensing+LCD+history | `src/main.cpp` (two `xTaskCreatePinnedToCore`), `cfg::task::kCore*` | core0Task=WiFi/HTTP/BLE/mail, core1Task=sense/alarm/LCD/history/beeper; mutex-guarded snapshot; task-affinity review (static stacks = Phase 6) | 4 |
| NFR-08 | OneWire via RMT; PWM via LEDC | `hal/onewire_bus_target`, `hal/pwm_target` | **DEVIATION**: OneWire bit-banged not RMT (owner-approved, ADR-10); PWM via LEDC per spec. HW oscilloscope check | 3 |
| NFR-09 | Stack protection; no secrets in git | Compiler flags; `.gitignore` | Code review + CI secret scan | 0 |
| NFR-10 | All HW behind HAL; domain testable on host | HAL link-time seam | `pio test -e native` passes without HW | 1 |
| NFR-11 | clang-format/tidy clean; ASan/UBSan; coverage ≥ 85 % | CI jobs | CI green on every commit | all |
| NFR-12 | No god objects; narrow module interfaces | All modules | Architect review; code review | 1 |
