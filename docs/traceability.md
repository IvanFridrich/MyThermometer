# Traceability Matrix — ESP32 Teplomer

Maps every FR and NFR from SPECIFICATION.md §2-3 to the module(s) that implement it
and the test method used to verify it.

Phase column: earliest phase where the requirement is satisfied.

## Functional Requirements

| ID    | Requirement (short) | Module(s) | Test method | Phase |
|-------|---------------------|-----------|-------------|-------|
| FR-01 | Measure both sensors 1×/min | `app/measurement_task`, `hal/onewire_bus` | HW smoke test (Phase 3) | 3 |
| FR-02 | 10-min moving average per sensor | `core/moving_average` | Unit test (MovingAverage) | 2 |
| FR-03 | Detect weird values (-30..80 °C), flag WEIRD_VALUE | `core/anomaly` | Unit test (AnomalyDetector) | 2 |
| FR-04 | Detect OneWire CRC/open, flag after 3 consecutive errors | `core/anomaly`, `hal/onewire_bus` | Unit test (AnomalyDetector error path) | 2 |
| FR-05 | Ring buffer 144 records, static RAM | `core/history_buffer` | Unit test (HistoryBuffer), static_assert 864 B | 2 |
| FR-06 | One record = two avg temps + OR flags per 10-min window | `core/history_buffer`, `app/measurement_task` | Unit test + integration | 2 |
| FR-07 | Diff alarm: \|avg_in−avg_out\| ≥ 2 °C → beep+flag, clear 1.5 °C | `core/alarm_state` | Unit test (AlarmState diff hysteresis) | 2 |
| FR-08 | Fire alarm: instant inner ≥ 45 °C → repeat alarm, clear 43 °C | `core/alarm_state` | Unit test (AlarmState fire hysteresis) | 2 |
| FR-09 | Sensor fault → distinctive beep + flag + email | `core/alarm_state`, `app/mail_task` | Unit test + integration | 2/5 |
| FR-10 | Fire detection only on inner sensor | `core/alarm_state` | Unit test (AlarmState snapshot.innerRaw only) | 2 |
| FR-11 | LCD row 1: temps; row 2: rotating pages (IP, uptime, status) | `app/lcd_task`, `hal/display` | HW smoke test | 3 |
| FR-12 | Show IP on boot | `app/lcd_task` | HW smoke test | 4 |
| FR-13 | Contrast via LEDC PWM | `hal/pwm` | HW smoke test | 3 |
| FR-14 | Connect to hardcoded SSID from secrets.h | `hal/wifi_hal` | HW integration test | 4 |
| FR-15 | Reconnect + exponential backoff + LCD "WiFi DN" | `hal/wifi_hal`, `app/web_task` | Integration (disconnect AP) | 4 |
| FR-16 | mDNS `teplomer.local` | `hal/wifi_hal` | Browser access test | 4 |
| FR-17 | Single-page web UI with history graph | `web/index.html`, `app/web_task` | Browser test | 7 |
| FR-18 | uPlot graph with missing-point rendering | `web/app.js` | Browser visual test | 7 |
| FR-19 | Web shows: uptime, errors, free heap, RSSI, sensor IDs | `web/app.js`, JSON API | Browser test | 7 |
| FR-20 | Config via web, persisted in NVS | `hal/nvs_store`, `app/web_task` | NVS round-trip test + browser | 4 |
| FR-21 | Web actions: restart, test beep, set contrast, test email, status email | `app/web_task` | Browser integration test | 4/5 |
| FR-22 | Web without authentication (LAN-trusted) | `hal/http_server` | Code review + design doc | 4 |
| FR-23 | JSON API endpoints | `app/web_task` | curl / browser test | 4 |
| FR-24 | BLE beacon 5 bursts/min, manufacturer data §6.2 | `hal/ble_advertiser`, `app/ble_task` | Python bleak monitor | 4 |
| FR-25 | UART log 115200, structured format | `core/event_log`, `app/measurement_task` | Terminal observation | 5 |
| FR-26 | Email on fire + sensor fault, rate-limited 1×/h | `app/mail_task`, `hal/mailer` | Unit test (email logic) + integration | 5 |
| FR-27 | All config fields persistent in NVS, default on empty | `hal/nvs_store`, `core/config_model` | Unit test (NvsStoreFake round-trip) | 4 |
| FR-28 | Python bleak monitor decodes advertising packets | `tools/ble_monitor/monitor.py` | Manual Windows test | 7 |

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
| NFR-08 | OneWire via RMT; PWM via LEDC | `hal/onewire_bus_target`, `hal/pwm_target` | HW oscilloscope check | 3 |
| NFR-09 | Stack protection; no secrets in git | Compiler flags; `.gitignore` | Code review + CI secret scan | 0 |
| NFR-10 | All HW behind HAL; domain testable on host | HAL link-time seam | `pio test -e native` passes without HW | 1 |
| NFR-11 | clang-format/tidy clean; ASan/UBSan; coverage ≥ 85 % | CI jobs | CI green on every commit | all |
| NFR-12 | No god objects; narrow module interfaces | All modules | Architect review; code review | 1 |
