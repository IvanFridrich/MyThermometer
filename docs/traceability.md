# Traceability Matrix — ESP32 Teplomer

Maps every FR and NFR from SPECIFICATION.md §2-3 to the module(s) that implement it
and the test method used to verify it.

Phase column: earliest phase where the requirement is satisfied.

## Functional Requirements

| ID    | Requirement (short) | Module(s) | Test method | Phase |
|-------|---------------------|-----------|-------------|-------|
| FR-01 | Measure both sensors 1×/min | `app/measurement_task`, `hal/onewire_bus_target` | Driver impl (Phase 3, bit-bang); HW smoke test pending; cadence in `app` (Phase 4) | 3 |
| FR-02 | 10-min moving average per sensor | `core/moving_average` | `test/test_moving_average` (warm-up, wrap, invalid exclusion) | 2 |
| FR-03 | Detect weird values (-30..80 °C), flag WEIRD_VALUE | `core/anomaly` | `test/test_anomaly` (bounds, sentinel, kTempInvalid) | 2 |
| FR-04 | Detect OneWire CRC/open, flag after 3 consecutive errors | `core/anomaly`, `hal/onewire_bus_target` | Driver returns kSensorOpen (no presence) / kOneWireErr (CRC, all-zero); 85 °C & −127 °C sentinel classification is in `core/anomaly` (ADR-12), `test/test_anomaly`; threshold/recovery `test/test_anomaly` + HW | 2 |
| FR-05 | Ring buffer 144 records, static RAM | `core/history_buffer` | `test/test_history_buffer` + `static_assert sizeof==6` | 2 |
| FR-06 | One record = two avg temps + OR flags per 10-min window | `core/history_buffer`, `app/measurement_task` | `test/test_history_buffer` (flags verbatim) + integration | 2 |
| FR-07 | Diff alarm: \|avg_out−avg_in\| ≥ 2 °C → beep+flag, clear 1.5 °C | `core/alarm_state`; window recommendation `core/window_advice` (D9) | `test/test_alarm_state` (diff hysteresis, both goals); `test/test_window_advice` (OPEN/CLOSE/NO_CHANGE truth table, 100% cov) | 2 |
| FR-08 | Fire alarm: instant inner ≥ 45 °C → repeat alarm, clear 43 °C | `core/alarm_state`; audio `core/beep_engine` (perfect-fourth fire pattern; re-armed while FIRE persists by the buzzer task, Phase 5) | `test/test_alarm_state` (fire hysteresis, no flap) + `test/test_beep_engine` (pattern timing/expiry, fake clock) | 2 |
| FR-09 | Sensor fault → distinctive beep + flag + email | `core/alarm_state`, `core/email_policy`, `app/mail_task` | `test/test_alarm_state` (sensor edges) + `test/test_email_policy` (§7 send decision) + integration | 2/5 |
| FR-10 | Fire detection only on inner sensor | `core/alarm_state` | `test/test_alarm_state` (innerRaw drives fire only) | 2 |
| FR-11 | LCD row 1: temps; row 2: rotating pages (IP, uptime, status) | `app/lcd_task`, `hal/display_target` | Driver impl (Phase 3, LiquidCrystal); pagination in `app` (Phase 4); HW smoke test | 3 |
| FR-12 | Show IP on boot | `app/lcd_task` | HW smoke test | 4 |
| FR-13 | Contrast via LEDC PWM | `hal/pwm_target` | Driver impl (Phase 3, LEDC ch1, 8-bit); HW smoke test pending | 3 |
| FR-14 | Connect to hardcoded SSID from secrets.h | `hal/wifi_hal_target` | Driver impl (Phase 4, WiFi STA); HW integration test | 4 |
| FR-15 | Reconnect + exponential backoff + LCD "WiFi DN" | `hal/wifi_hal_target`, `app/web_task` | Driver primitives (begin/isConnected); backoff state machine in `app` (Phase 4); integration (disconnect AP) | 4 |
| FR-16 | mDNS `teplomer.local` | `hal/wifi_hal_target` | Driver impl (Phase 4, ESPmDNS + http service); browser access test | 4 |
| FR-17 | Single-page web UI with history graph | `web/index.html`, `app/web_task` | Browser test | 7 |
| FR-18 | uPlot graph with missing-point rendering | `web/app.js` | Browser visual test | 7 |
| FR-19 | Web shows: uptime, errors, free heap, RSSI, sensor IDs | `web/app.js`, `core/json_api` (`/api/current` fields) | `test/test_json_api` (exact shape/values); browser test | 7 |
| FR-20 | Config via web, persisted in NVS | `hal/nvs_store_target`, `app/web_task` | Driver impl (Phase 4, Preferences); NVS round-trip + browser | 4 |
| FR-21 | Web actions: restart, test beep, set contrast, test email, status email | `app/web_task` | Browser integration test | 4/5 |
| FR-22 | Web without authentication (LAN-trusted) | `hal/http_server_target` | Driver impl (Phase 4, plain HTTP no auth); code review + design doc | 4 |
| FR-23 | JSON API endpoints | `hal/http_server_target`, `core/json_api`, `app/web_task` | `core/json_api` serializers for `/api/current` + `/api/history` with `test/test_json_api` (exact-string, null-on-invalid, bounded-buffer, 88% cov); route registration (Phase 4); handlers wire in `app` | 4 |
| FR-24 | BLE beacon 5 bursts/min, manufacturer data §6.2 | `hal/ble_advertiser_target`, `core/ble_payload`, `app/ble_task` | Driver impl (Phase 4, NimBLE non-conn); §6.2 encoder `core/ble_payload` with byte-exact `test/test_ble_payload` (100% cov); burst cadence in `app/ble_task`; Python bleak monitor | 4 |
| FR-25 | UART log 115200, structured format | `core/event_log`, `app/measurement_task` | Terminal observation | 5 |
| FR-26 | Email on fire + sensor fault, rate-limited 1×/h | `core/email_policy`, `app/mail_task`, `hal/mailer` | `test/test_email_policy` (§7: rising-edge only, 1/interval/type, persistence silent, clear+re-arm, disabled, wrap; 100% cov); SMTP send + integration Phase 5 | 5 |
| FR-27 | All config fields persistent in NVS, default on empty | `hal/nvs_store_target`, `core/config_model` | Driver impl (Phase 4, Preferences typed get/put); `test/test_config_model` + NvsStoreFake round-trip | 4 |
| FR-28 | Python bleak monitor decodes advertising packets | `tools/ble_monitor/monitor.py` | `--selftest` decodes §6.2 vectors (byte-matched to firmware encoder); manual Windows scan | 7 |

## Non-Functional Requirements

| ID     | Requirement (short) | Module(s) / Mechanism | Verified by | Phase |
|--------|---------------------|-----------------------|-------------|-------|
| NFR-01 | Survives WiFi outage, sensor detach, voltage dip | `app/supervisor`, reconnect logic | Fault injection tests (Phase 6) | 6 |
| NFR-02 | Task WDT, 8 s timeout, reset on hang | `hal/system_hal`, `app/supervisor` | Artificial deadlock test (Phase 6) | 6 |
| NFR-03 | Brownout detector enabled, clean reset | ESP-IDF sdkconfig / `hal/system_hal` | Power supply voltage ramp test | 6 |
| NFR-04 | No heap alloc in steady state; static stacks/buffers | All modules (ETL, static arrays) | ASan/UBSan + min_free_heap monitor | all |
| NFR-05 | `-fno-exceptions -fno-rtti`; errors via `Result<T>` | `include/result.h` + all `.cpp` | Build flags; code review | 0 |
| NFR-06 | No runtime virtual dispatch; link-time seam | HAL headers (no vtable), build_src_filter | Static analysis; code review | 1 |
| NFR-07 | Core 0: WiFi+BLE; Core 1: sensing+LCD+history | `app/*_task`, `cfg::task::kCore*` | Task affinity review | 4 |
| NFR-08 | OneWire via RMT; PWM via LEDC | `hal/onewire_bus_target`, `hal/pwm_target` | **DEVIATION**: OneWire bit-banged not RMT (owner-approved, ADR-10); PWM via LEDC per spec. HW oscilloscope check | 3 |
| NFR-09 | Stack protection; no secrets in git | Compiler flags; `.gitignore` | Code review + CI secret scan | 0 |
| NFR-10 | All HW behind HAL; domain testable on host | HAL link-time seam | `pio test -e native` passes without HW | 1 |
| NFR-11 | clang-format/tidy clean; ASan/UBSan; coverage ≥ 85 % | CI jobs | CI green on every commit | all |
| NFR-12 | No god objects; narrow module interfaces | All modules | Architect review; code review | 1 |
