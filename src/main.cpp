// main.cpp — firmware composition for the ESP32-S3 thermometer.
//
// Two FreeRTOS tasks pinned per NFR-07 / §8.5:
//   Core 1 (kCoreApp): sensing, moving average, anomaly, alarms, history, LCD, beeper.
//   Core 0 (kCoreNet): WiFi lifecycle + reconnect, HTTP server, BLE beacon, email.
// They exchange a mutex-guarded snapshot. The domain logic lives in core/* (host
// tested); this file is the (hardware-only) glue. The HttpServer HAL is too thin
// to serve responses, so the web task uses Arduino WebServer directly while the
// WiFi/NVS/BLE HALs and every core/* module are used as-is.
//
// Verified to COMPILE/LINK for esp32-s3; on-hardware behaviour is the owner's
// acceptance step (join AP, page, beacon, reconnect, sensor detach, WDT reset).

#include <Arduino.h>
#include <LittleFS.h> // pulled in so ESP-Mail-Client's flash-FS layer links (no attachments used)
#include <WebServer.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <uri/UriBraces.h>

#include "Config.h"
#include "alarm_state.h"
#include "anomaly.h"
#include "beep_engine.h"
#include "ble_payload.h"
#include "config_model.h"
#include "email_policy.h"
#include "event_log.h"
#include "history_buffer.h"
#include "json_api.h"
#include "moving_average.h"
#include "types.h"
#include "window_advice.h"

#include "hal/ble_advertiser.h"
#include "hal/display.h"
#include "hal/mailer.h"
#include "hal/nvs_store.h"
#include "hal/onewire_bus.h"
#include "hal/pwm.h"
#include "hal/system_hal.h"
#include "hal/wifi_hal.h"

#include "app/web_assets.h"
#include "secrets.h"

// ---------------------------------------------------------------------------
// HAL + domain instances (constructed once; live for the program lifetime)
// ---------------------------------------------------------------------------
namespace {

OneWireBus g_innerBus(cfg::pin::kOneWireInside);
OneWireBus g_outerBus(cfg::pin::kOneWireOutside);
Display    g_lcd(cfg::pin::kLcdRs, cfg::pin::kLcdEn, cfg::pin::kLcdD4, cfg::pin::kLcdD5,
                 cfg::pin::kLcdD6, cfg::pin::kLcdD7);
Pwm        g_pwm;
WifiHal    g_wifi;
NvsStore   g_nvs("thermo");
BleAdvertiser g_ble;
SystemHal     g_sys;
Mailer        g_mailer(SMTP_HOST, cfg::email::kSmtpPort, SMTP_USER, SMTP_PASS, SMTP_USER);

ConfigModel                                 g_config = ConfigModel::defaults();
AlarmState                                  g_alarm(g_config); // holds g_config by reference
HistoryBuffer                               g_history;
EventLog                                    g_log;
email_policy::EmailPolicy                   g_email;
beep::BeepEngine                            g_beep;
MovingAverage<cfg::sample::kAvgWindowSamples> g_innerAvg;
MovingAverage<cfg::sample::kAvgWindowSamples> g_outerAvg;
AnomalyDetector                             g_innerAnomaly;
AnomalyDetector                             g_outerAnomaly;

WebServer g_server(cfg::net::kHttpPort);

// Latest values shared Core1 -> Core0, guarded by a short critical section.
portMUX_TYPE        g_mux = portMUX_INITIALIZER_UNLOCKED;
json_api::CurrentStatus g_snapshot;

// Static scratch for JSON responses (no heap; one writer per buffer at a time).
char g_jsonCurrent[cfg::net::kJsonCurrentBufSize];
char g_jsonHistory[cfg::net::kJsonHistoryBufSize];

uint8_t g_bleSeq = 0;

// NVS keys (§6.4).
constexpr char kKeyBeeper[]    = "beeper";
constexpr char kKeyDiffThr[]   = "diff_thr";
constexpr char kKeyDiffHyst[]  = "diff_hyst";
constexpr char kKeyFireThr[]   = "fire_thr";
constexpr char kKeyFireHyst[]  = "fire_hyst";
constexpr char kKeyContrast[]  = "contrast";
constexpr char kKeyEmail[]     = "email";
constexpr char kKeyGoal[]      = "win_goal";

void logLine(cfg::log::Level level, const char* module, const char* msg) {
    const uint32_t up = millis();
    g_log.append(level, module, msg, up);
    Serial.printf("[+%lu][%d][%s] %s\n", static_cast<unsigned long>(up),
                  static_cast<int>(level), module, msg);
}

// --- config persistence -----------------------------------------------------
void loadConfig() {
    if (!g_nvs.open().isOk()) {
        logLine(cfg::log::Level::kWarn, "NVS", "open failed; using defaults");
        return;
    }
    g_config.beeperEnabled      = g_nvs.getBool(kKeyBeeper, g_config.beeperEnabled).value();
    g_config.diffThresholdC100  = g_nvs.getInt16(kKeyDiffThr, g_config.diffThresholdC100).value();
    g_config.diffHysteresisC100 = g_nvs.getInt16(kKeyDiffHyst, g_config.diffHysteresisC100).value();
    g_config.fireThrC100        = g_nvs.getInt16(kKeyFireThr, g_config.fireThrC100).value();
    g_config.fireHysteresisC100 = g_nvs.getInt16(kKeyFireHyst, g_config.fireHysteresisC100).value();
    g_config.lcdContrastPwm     = g_nvs.getUint8(kKeyContrast, g_config.lcdContrastPwm).value();
    g_config.emailEnabled       = g_nvs.getBool(kKeyEmail, g_config.emailEnabled).value();
    g_config.windowGoal         = g_nvs.getUint8(kKeyGoal, g_config.windowGoal).value();
    if (!g_config.validate()) {
        g_config = ConfigModel::defaults();
        logLine(cfg::log::Level::kWarn, "NVS", "stored config invalid; reset to defaults");
    }
}

void saveConfig() {
    g_nvs.putBool(kKeyBeeper, g_config.beeperEnabled);
    g_nvs.putInt16(kKeyDiffThr, g_config.diffThresholdC100);
    g_nvs.putInt16(kKeyDiffHyst, g_config.diffHysteresisC100);
    g_nvs.putInt16(kKeyFireThr, g_config.fireThrC100);
    g_nvs.putInt16(kKeyFireHyst, g_config.fireHysteresisC100);
    g_nvs.putUint8(kKeyContrast, g_config.lcdContrastPwm);
    g_nvs.putBool(kKeyEmail, g_config.emailEnabled);
    g_nvs.putUint8(kKeyGoal, g_config.windowGoal);
}

json_api::CurrentStatus snapshotCopy() {
    json_api::CurrentStatus copy;
    taskENTER_CRITICAL(&g_mux);
    copy = g_snapshot;
    taskEXIT_CRITICAL(&g_mux);
    return copy;
}

// ---------------------------------------------------------------------------
// HTTP handlers (Core 0). Registered as captureless lambdas using file globals.
// ---------------------------------------------------------------------------
void handleIndex() {
    g_server.send_P(200, "text/html", web_assets::kIndexHtml);
}
void handleAppJs() {
    g_server.send_P(200, "application/javascript", web_assets::kAppJs);
}
void handleApiCurrent() {
    const json_api::CurrentStatus s = snapshotCopy();
    const size_t n = json_api::serializeCurrent(s, g_jsonCurrent, sizeof(g_jsonCurrent));
    if (n == 0) {
        g_server.send(503, "text/plain", "busy");
        return;
    }
    g_server.send(200, "application/json", g_jsonCurrent);
}
void handleApiHistory() {
    const uint32_t uptimeS = millis() / 1000UL;
    size_t         n       = 0;
    taskENTER_CRITICAL(&g_mux);
    n = json_api::serializeHistory(g_history, uptimeS, g_jsonHistory, sizeof(g_jsonHistory));
    taskEXIT_CRITICAL(&g_mux);
    if (n == 0) {
        g_server.send(503, "text/plain", "busy");
        return;
    }
    g_server.send(200, "application/json", g_jsonHistory);
}

int16_t argI16(const char* name, int16_t fallback) {
    return g_server.hasArg(name) ? static_cast<int16_t>(g_server.arg(name).toInt()) : fallback;
}

void handleApiConfig() {
    ConfigModel next = g_config;
    next.beeperEnabled      = argI16(kKeyBeeper, next.beeperEnabled ? 1 : 0) != 0;
    next.emailEnabled       = argI16("email", next.emailEnabled ? 1 : 0) != 0;
    next.windowGoal         = static_cast<uint8_t>(argI16("window_goal", next.windowGoal));
    next.diffThresholdC100  = argI16("diff_thr_c100", next.diffThresholdC100);
    next.diffHysteresisC100 = argI16("diff_hyst_c100", next.diffHysteresisC100);
    next.fireThrC100        = argI16("fire_thr_c100", next.fireThrC100);
    next.fireHysteresisC100 = argI16("fire_hyst_c100", next.fireHysteresisC100);
    next.lcdContrastPwm     = static_cast<uint8_t>(argI16("contrast", next.lcdContrastPwm));
    if (!next.validate()) {
        g_server.send(400, "text/plain", "invalid config");
        return;
    }
    g_config = next;
    saveConfig();
    g_pwm.setContrastDuty(g_config.lcdContrastPwm);
    logLine(cfg::log::Level::kInfo, "WEB", "config updated");
    g_server.send(200, "text/plain", "ok");
}

void handleAction() {
    const String action = g_server.pathArg(0);
    if (action == "test-beep") {
        g_beep.playTone(cfg::beep::kTestToneHz, millis());
    } else if (action == "set-contrast") {
        g_pwm.setContrastDuty(static_cast<uint8_t>(argI16("contrast", g_config.lcdContrastPwm)));
    } else if (action == "test-email" || action == "status-email") {
        if (!g_config.emailEnabled) {
            g_server.send(409, "text/plain", "email disabled");
            return;
        }
        // Manual sends bypass the §7 rate limiter.
        esp_task_wdt_delete(nullptr); // SMTP may exceed the WDT window
        const bool ok = g_mailer.send(RECIPIENT_EMAIL, "Teplomer: status", "Manual status email")
                            .isOk();
        esp_task_wdt_add(nullptr);
        g_server.send(ok ? 200 : 502, "text/plain", ok ? "sent" : "send failed");
        return;
    } else if (action == "restart") {
        g_server.send(200, "text/plain", "restarting");
        delay(200);
        g_sys.restart();
        return;
    } else {
        g_server.send(404, "text/plain", "unknown action");
        return;
    }
    g_server.send(200, "text/plain", "ok");
}

// ---------------------------------------------------------------------------
// LCD rendering (Core 1)
// ---------------------------------------------------------------------------
void renderLcd(const json_api::CurrentStatus& s, uint32_t nowMs) {
    char line[cfg::lcd::kCols + 1];
    const bool showInner = (nowMs / cfg::lcd::kRefreshMs) % 2 == 0;
    if (showInner) {
        if (s.innerC100 == kTempInvalid) {
            snprintf(line, sizeof(line), "I: --.-");
        } else {
            snprintf(line, sizeof(line), "I:%.1f", s.innerC100 / 100.0);
        }
    } else {
        if (s.outerC100 == kTempInvalid) {
            snprintf(line, sizeof(line), "O: --.-");
        } else {
            snprintf(line, sizeof(line), "O:%.1f", s.outerC100 / 100.0);
        }
    }
    g_lcd.setCursor(0, 0);
    g_lcd.print(line);

    char row1[cfg::lcd::kCols + 1];
    if (s.fire) {
        snprintf(row1, sizeof(row1), "FIRE!");
    } else if (s.sensorFault) {
        snprintf(row1, sizeof(row1), "SENSOR");
    } else if (!g_wifi.isConnected()) {
        snprintf(row1, sizeof(row1), "WiFi DN");
    } else {
        const uint32_t up = nowMs / 1000UL;
        snprintf(row1, sizeof(row1), "up%lum", static_cast<unsigned long>(up / 60UL));
    }
    g_lcd.setCursor(0, 1);
    g_lcd.print(row1);
}

// ---------------------------------------------------------------------------
// Measurement + alarm cycle (Core 1)
// ---------------------------------------------------------------------------
Temperature readSensor(OneWireBus& bus, AnomalyDetector& det, MovingAverage<cfg::sample::kAvgWindowSamples>& avg,
                       EventFlags& flagsOut) {
    const Result<Temperature> r = bus.readCentiC();
    flagsOut                    = det.classify(r);
    if (r.isOk() && (flagsOut & cfg::flag::kWeirdValue) == 0U) {
        avg.push(r.value());
    } else {
        avg.push(kTempInvalid);
    }
    return r.isOk() ? r.value() : kTempInvalid;
}

void measurementCycle(uint32_t nowMs) {
    EventFlags innerFlags = 0;
    EventFlags outerFlags = 0;
    const Temperature innerRaw = readSensor(g_innerBus, g_innerAnomaly, g_innerAvg, innerFlags);
    (void)readSensor(g_outerBus, g_outerAnomaly, g_outerAvg, outerFlags);

    const Temperature innerAvg = g_innerAvg.average();
    const Temperature outerAvg = g_outerAvg.average();
    const EventFlags  flags    = static_cast<EventFlags>(innerFlags | outerFlags);

    const MeasurementSnapshot ms{innerRaw, innerAvg, outerAvg, flags};
    const AlarmEdges          edges = g_alarm.update(ms);

    // Drive the buzzer on rising edges (fire re-armed below while it persists).
    if (g_beep.enabled() != g_config.beeperEnabled) {
        g_beep.setEnabled(g_config.beeperEnabled);
    }
    if (edges.fireRising) {
        g_beep.playFire(nowMs);
        logLine(cfg::log::Level::kError, "ALARM", "fire rising");
    } else if (edges.sensorRising) {
        g_beep.playTone(cfg::beep::kSensorToneHz, nowMs);
        logLine(cfg::log::Level::kError, "ALARM", "sensor fault");
    } else if (edges.diffRising) {
        g_beep.playTone(cfg::beep::kDiffToneHz, nowMs);
    }
    if (g_alarm.isFire() && !g_beep.isActive()) {
        g_beep.playFire(nowMs); // keep sounding while the fire condition holds (FR-08)
    }

    const auto advice = window::advise(innerAvg, outerAvg,
                                       static_cast<cfg::window_advisor::Goal>(g_config.windowGoal),
                                       g_config.diffThresholdC100);

    json_api::CurrentStatus s;
    s.innerC100    = innerAvg;
    s.outerC100    = outerAvg;
    s.windowAdvice = advice;
    s.windowGoal   = g_config.windowGoal;
    s.fire         = g_alarm.isFire();
    s.sensorFault  = g_alarm.isSensorFault();
    s.diffAlarm    = g_alarm.isDiff();
    s.uptimeS      = nowMs / 1000UL;
    s.freeHeap     = g_sys.freeHeap();
    s.minFreeHeap  = g_sys.minFreeHeap();
    s.rssi         = g_wifi.rssi();
    s.beeperEnabled = g_config.beeperEnabled;
    s.emailEnabled  = g_config.emailEnabled;
    s.fireThrC100   = g_config.fireThrC100;
    s.fireHystC100  = g_config.fireHysteresisC100;
    s.diffThrC100   = g_config.diffThresholdC100;
    s.diffHystC100  = g_config.diffHysteresisC100;
    s.contrast      = g_config.lcdContrastPwm;
    // ROM IDs are read once at boot (see setup()); carry them forward.
    taskENTER_CRITICAL(&g_mux);
    s.innerRom = g_snapshot.innerRom;
    s.outerRom = g_snapshot.outerRom;
    g_snapshot = s;
    taskEXIT_CRITICAL(&g_mux);
}

void appendHistory() {
    const json_api::CurrentStatus s = snapshotCopy();
    uint16_t                      f = 0;
    if (s.fire) f |= cfg::flag::kFire;
    if (s.sensorFault) f |= cfg::flag::kSensorOpen;
    if (s.diffAlarm) f |= cfg::flag::kDiffExceeded;
    if (s.innerC100 == kTempInvalid) f |= cfg::flag::kInnerInvalid;
    if (s.outerC100 == kTempInvalid) f |= cfg::flag::kOuterInvalid;

    HistoryRecord rec{};
    rec.t_inner_c100 = g_innerAvg.average();
    rec.t_outer_c100 = g_outerAvg.average();
    rec.flags        = f;
    taskENTER_CRITICAL(&g_mux);
    g_history.append(rec);
    taskEXIT_CRITICAL(&g_mux);
}

[[noreturn]] void core1Task(void*) {
    esp_task_wdt_add(nullptr);
    uint32_t lastMeasure = 0;
    uint32_t lastHistory = 0;
    uint32_t lastLcd     = 0;
    measurementCycle(millis()); // prime (first OneWire read returns kNotReady)
    for (;;) {
        const uint32_t now = millis();
        if (now - lastMeasure >= cfg::sample::kSamplePeriodMs) {
            lastMeasure = now;
            measurementCycle(now);
        }
        if (now - lastHistory >= cfg::sample::kHistoryStrideMs) {
            lastHistory = now;
            appendHistory();
        }
        if (now - lastLcd >= cfg::lcd::kRefreshMs) {
            lastLcd = now;
            renderLcd(snapshotCopy(), now);
        }
        const uint16_t hz = g_beep.tick(now);
        if (hz != 0) {
            g_pwm.tone(hz);
        } else {
            g_pwm.noTone();
        }
        g_sys.wdtFeed();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ---------------------------------------------------------------------------
// Connectivity cycle (Core 0): WiFi reconnect, web, BLE, email
// ---------------------------------------------------------------------------
void sendBleBeacon(const json_api::CurrentStatus& s) {
    EventFlags ev = 0;
    if (s.fire) ev |= cfg::flag::kFire;
    if (s.sensorFault) ev |= cfg::flag::kSensorOpen;
    if (s.diffAlarm) ev |= cfg::flag::kDiffExceeded;
    const auto payload = ble_payload::encode(s.innerC100, s.outerC100, ev, g_bleSeq++);
    g_ble.setPayload(payload.data(), static_cast<uint8_t>(payload.size()));
    g_ble.burst(cfg::ble::kBurstsPerMinute, cfg::ble::kBurstSpacingMs);
}

void checkEmail(const json_api::CurrentStatus& s, uint32_t nowMs) {
    const bool wantFire   = g_email.onAlarm(email_policy::Type::kFire, s.fire, s.emailEnabled, nowMs);
    const bool wantSensor =
        g_email.onAlarm(email_policy::Type::kSensorFault, s.sensorFault, s.emailEnabled, nowMs);
    if (!wantFire && !wantSensor) {
        return;
    }
    const char* subject = wantFire ? "Teplomer: POZAR" : "Teplomer: porucha cidla";
    esp_task_wdt_delete(nullptr);
    const bool ok = g_mailer.send(RECIPIENT_EMAIL, subject, "Alarm raised").isOk();
    esp_task_wdt_add(nullptr);
    logLine(ok ? cfg::log::Level::kInfo : cfg::log::Level::kError, "MAIL",
            ok ? "alarm email sent" : "alarm email failed");
}

[[noreturn]] void core0Task(void*) {
    esp_task_wdt_add(nullptr);
    uint32_t backoffMs    = cfg::net::kReconnectMinMs;
    uint32_t lastWifiTry  = 0;
    uint32_t lastBle      = 0;
    uint32_t lastEmail    = 0;
    bool     mdnsStarted  = false;
    for (;;) {
        const uint32_t now = millis();

        if (!g_wifi.isConnected()) {
            mdnsStarted = false;
            if (now - lastWifiTry >= backoffMs) {
                lastWifiTry = now;
                logLine(cfg::log::Level::kWarn, "WIFI", "connecting");
                g_wifi.begin(WIFI_SSID, WIFI_PASSWORD);
                backoffMs = (backoffMs * 2U > cfg::net::kReconnectMaxMs) ? cfg::net::kReconnectMaxMs
                                                                        : backoffMs * 2U;
            }
        } else {
            backoffMs = cfg::net::kReconnectMinMs;
            if (!mdnsStarted) {
                g_wifi.startMdns(cfg::net::kMdnsHostname);
                mdnsStarted = true;
                char ip[16];
                g_wifi.getIp(ip, sizeof(ip));
                logLine(cfg::log::Level::kInfo, "WIFI", ip);
            }
            g_server.handleClient();
        }

        const json_api::CurrentStatus s = snapshotCopy();
        if (now - lastBle >= cfg::sample::kSamplePeriodMs) {
            lastBle = now;
            sendBleBeacon(s);
        }
        if (now - lastEmail >= cfg::net::kWifiCheckMs) {
            lastEmail = now;
            checkEmail(s, now);
        }

        g_sys.wdtFeed();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

} // namespace

void setup() {
    Serial.begin(cfg::log::kBaud);
    delay(200);
    logLine(cfg::log::Level::kInfo, "BOOT", "thermometer starting");

    if (g_sys.resetReason() == ResetReason::kBrownout) {
        logLine(cfg::log::Level::kWarn, "BOOT", "recovered from brownout");
    }

    g_lcd.init();
    g_pwm.initContrast(cfg::pin::kLcdContrastV0, cfg::ledc::kContrastChannel);
    g_pwm.initBuzzer(cfg::pin::kBuzzer, cfg::ledc::kBuzzerChannel);

    loadConfig();
    g_beep.setEnabled(g_config.beeperEnabled);
    g_pwm.setContrastDuty(g_config.lcdContrastPwm);

    // Read each sensor's ROM ID once for diagnostics.
    const Result<uint64_t> innerRom = g_innerBus.readRomId();
    const Result<uint64_t> outerRom = g_outerBus.readRomId();
    g_snapshot.innerRom = innerRom.isOk() ? innerRom.value() : 0;
    g_snapshot.outerRom = outerRom.isOk() ? outerRom.value() : 0;

    g_ble.init(cfg::ble::kDeviceName, cfg::ble::kCompanyId);

    g_server.on("/", HTTP_GET, handleIndex);
    g_server.on("/app.js", HTTP_GET, handleAppJs);
    g_server.on("/api/current", HTTP_GET, handleApiCurrent);
    g_server.on("/api/history", HTTP_GET, handleApiHistory);
    g_server.on("/api/config", HTTP_POST, handleApiConfig);
    g_server.on(UriBraces("/api/action/{}"), HTTP_POST, handleAction);
    g_server.begin();

    g_beep.playTone(cfg::beep::kBootToneHz, millis());

    // Task WDT: 8 s, panic-reset (NFR-02). Each task registers + feeds itself.
    esp_task_wdt_init(cfg::safety::kWdtTimeoutMs / 1000U, cfg::safety::kPanicOnWdt);

    xTaskCreatePinnedToCore(core1Task, "core1", cfg::task::kStackSensor, nullptr,
                            cfg::task::kPrioSensor, nullptr, cfg::task::kCoreApp);
    xTaskCreatePinnedToCore(core0Task, "core0", cfg::task::kStackWeb, nullptr,
                            cfg::task::kPrioWeb, nullptr, cfg::task::kCoreNet);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000)); // all work runs in the pinned tasks
}
