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

#include <NimBLEDevice.h>
#include <esp_https_server.h>
#include <esp_ota_ops.h>
#include "certs.h"

// ---------------------------------------------------------------------------
// HAL + domain instances (constructed once; live for the program lifetime)
// ---------------------------------------------------------------------------
namespace {

OneWireBus    g_innerBus(cfg::pin::kOneWireInside);
OneWireBus    g_outerBus(cfg::pin::kOneWireOutside);
Display       g_lcd(cfg::pin::kLcdRs, cfg::pin::kLcdEn, cfg::pin::kLcdD4, cfg::pin::kLcdD5,
                    cfg::pin::kLcdD6, cfg::pin::kLcdD7);
Pwm           g_pwm;
WifiHal       g_wifi;
NvsStore      g_nvs("thermo");
BleAdvertiser g_ble;
SystemHal     g_sys;
Mailer        g_mailer(SMTP_HOST, cfg::email::kSmtpPort, SMTP_USER, SMTP_PASS, SMTP_USER);

// g_config is Core-1-owned (AlarmState reads it by reference); the web task edits
// g_webConfig under g_mux, and Core 1 latches it into g_config once per cycle.
ConfigModel                                   g_config    = ConfigModel::defaults();
ConfigModel                                   g_webConfig = ConfigModel::defaults();
AlarmState                                    g_alarm(g_config); // holds g_config by reference
HistoryBuffer                                 g_history;
EventLog                                      g_log;
email_policy::EmailPolicy                     g_email;
beep::BeepEngine                              g_beep;
MovingAverage<cfg::sample::kAvgWindowSamples> g_innerAvg;
MovingAverage<cfg::sample::kAvgWindowSamples> g_outerAvg;
AnomalyDetector                               g_innerAnomaly;
AnomalyDetector                               g_outerAnomaly;

WebServer g_server(cfg::net::kHttpPort);

// Latest values shared Core1 -> Core0, guarded by a short critical section.
portMUX_TYPE            g_mux = portMUX_INITIALIZER_UNLOCKED;
json_api::CurrentStatus g_snapshot;

// Static scratch for JSON responses and email bodies (no heap; one writer each).
char g_jsonCurrent[cfg::net::kJsonCurrentBufSize];
char g_jsonHistory[cfg::net::kJsonHistoryBufSize];
char g_emailBody[cfg::email::kBodyBufSize]; // written only on Core 0 web task
// Copy of the ring buffer taken under g_mux so /api/history serializes outside the
// critical section (the serialize loop is too long to hold the spinlock).
HistoryBuffer g_histScratch;

// Event flags accumulated during the current 10-min history window from BOTH cores
// (email sent/failed, brownout-recover). Guarded by g_mux; folded into each record.
EventFlags g_windowFlags = 0;

uint8_t g_bleSeq = 0;

httpd_handle_t g_httpsServer{nullptr};

// Static task stacks + TCBs (NFR-04: no heap in steady state; NFR-07).
// StackType_t is uint8_t on Xtensa/ESP32-S3, so array sizes equal byte counts.
StaticTask_t s_core0Tcb;
StaticTask_t s_core1Tcb;
StackType_t  s_core0Stack[cfg::task::kStackWeb];
StackType_t  s_core1Stack[cfg::task::kStackSensor];

// NVS keys (§6.4).
constexpr char kKeyBeeper[]   = "beeper";
constexpr char kKeyDiffThr[]  = "diff_thr";
constexpr char kKeyDiffHyst[] = "diff_hyst";
constexpr char kKeyFireThr[]  = "fire_thr";
constexpr char kKeyFireHyst[] = "fire_hyst";
constexpr char kKeyContrast[] = "contrast";
constexpr char kKeyEmail[]    = "email";
constexpr char kKeyGoal[]     = "win_goal";

void logLine(cfg::log::Level level, const char* module, const char* msg) {
    const uint32_t up = millis();
    g_log.append(level, module, msg, up);
    Serial.printf("[+%lu][%d][%s] %s\n", static_cast<unsigned long>(up), static_cast<int>(level),
                  module, msg);
}

// ---------------------------------------------------------------------------
// BLE GATT OTA service
// Protocol: CTRL write [0x01, size LE u32] → DATA writes (chunks) →
//           CTRL write [0x02] → STATUS notify [0x00] → restart.
//           CTRL write [0x03] aborts at any point.
// Callbacks run in the NimBLE task (not Task-WDT-registered; no feed needed).
// ---------------------------------------------------------------------------
struct BleOtaCtx {
    esp_ota_handle_t       handle{};
    const esp_partition_t* target{nullptr};
    uint32_t               expected{0};
    uint32_t               received{0};
    bool                   active{false};
};
static BleOtaCtx              s_bleOta;
static NimBLECharacteristic*  s_bleOtaStatus{nullptr};

static void bleOtaNotify(uint8_t code) {
    if (s_bleOtaStatus != nullptr) {
        s_bleOtaStatus->notify(&code, 1);
    }
}

struct OtaCtrlCb final : NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& /*info*/) override {
        const NimBLEAttValue val = c->getValue();
        const uint8_t*       d   = val.data();
        const size_t         len = val.size();
        if (len == 0) {
            return;
        }
        if (d[0] == 0x01U && len == 5U) {
            // Start OTA: [0x01, firmware_size_le_u32]
            const uint32_t sz = static_cast<uint32_t>(d[1]) |
                                (static_cast<uint32_t>(d[2]) << 8U) |
                                (static_cast<uint32_t>(d[3]) << 16U) |
                                (static_cast<uint32_t>(d[4]) << 24U);
            s_bleOta.target = esp_ota_get_next_update_partition(nullptr);
            if (s_bleOta.target == nullptr ||
                esp_ota_begin(s_bleOta.target, sz, &s_bleOta.handle) != ESP_OK) {
                logLine(cfg::log::Level::kError, "BLE-OTA", "begin failed");
                bleOtaNotify(0xFFU);
                return;
            }
            s_bleOta.expected = sz;
            s_bleOta.received = 0;
            s_bleOta.active   = true;
            logLine(cfg::log::Level::kInfo, "BLE-OTA", "started");
        } else if (d[0] == 0x02U) {
            // Commit: verify + set boot partition + restart
            if (!s_bleOta.active) {
                bleOtaNotify(0xFFU);
                return;
            }
            s_bleOta.active = false;
            if (esp_ota_end(s_bleOta.handle) != ESP_OK) {
                logLine(cfg::log::Level::kError, "BLE-OTA", "verify failed");
                bleOtaNotify(0xFFU);
                return;
            }
            esp_ota_set_boot_partition(s_bleOta.target);
            logLine(cfg::log::Level::kInfo, "BLE-OTA", "success, restarting");
            bleOtaNotify(0x00U);
            // Must not call esp_restart() here: onWrite runs in the BLE task,
            // so blocking here prevents NimBLE from sending the ATT Write Response
            // and the STATUS notify. Spawn a low-priority task so onWrite returns
            // first, then restart 400 ms later once NimBLE has flushed.
            xTaskCreate([](void*) {
                vTaskDelay(pdMS_TO_TICKS(400));
                esp_restart();
            }, "ota_rst", 2048, nullptr, tskIDLE_PRIORITY + 1, nullptr);
        } else if (d[0] == 0x03U) {
            // Abort
            if (s_bleOta.active) {
                esp_ota_abort(s_bleOta.handle);
                s_bleOta.active = false;
                logLine(cfg::log::Level::kInfo, "BLE-OTA", "aborted");
            }
        }
    }
};

struct OtaDataCb final : NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& /*info*/) override {
        if (!s_bleOta.active) {
            return;
        }
        const NimBLEAttValue val = c->getValue();
        const uint8_t*       d   = val.data();
        const size_t         len = val.size();
        if (len == 0) {
            return;
        }
        if (esp_ota_write(s_bleOta.handle, d, len) != ESP_OK) {
            esp_ota_abort(s_bleOta.handle);
            s_bleOta.active = false;
            logLine(cfg::log::Level::kError, "BLE-OTA", "write failed");
            bleOtaNotify(0xFFU);
            return;
        }
        s_bleOta.received += static_cast<uint32_t>(len);
    }
};

static OtaCtrlCb s_otaCtrlCb;
static OtaDataCb s_otaDataCb;

void setupBleOtaGatt() {
    NimBLEServer*  srv = NimBLEDevice::createServer();
    NimBLEService* svc = srv->createService(cfg::ble::kOtaSvcUuid);

    NimBLECharacteristic* ctrl =
        svc->createCharacteristic(cfg::ble::kOtaCtrlUuid, NIMBLE_PROPERTY::WRITE);
    ctrl->setCallbacks(&s_otaCtrlCb);

    NimBLECharacteristic* data =
        svc->createCharacteristic(cfg::ble::kOtaDataUuid, NIMBLE_PROPERTY::WRITE_NR);
    data->setCallbacks(&s_otaDataCb);

    s_bleOtaStatus =
        svc->createCharacteristic(cfg::ble::kOtaStatusUuid, NIMBLE_PROPERTY::NOTIFY);

    // NimBLE 2.x: services start automatically when advertising begins
    logLine(cfg::log::Level::kInfo, "BLE-OTA", "GATT service registered");
}

// OTA POST handler — runs in esp_https_server's own task (not registered with Task WDT).
esp_err_t handleOtaPost(httpd_req_t* req) {
    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    if (target == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no OTA partition");
        return ESP_FAIL;
    }
    esp_ota_handle_t handle{};
    if (esp_ota_begin(target, req->content_len, &handle) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_begin failed");
        return ESP_FAIL;
    }
    static char otaBuf[cfg::net::kOtaRecvBufSize];
    int         remaining = static_cast<int>(req->content_len);
    while (remaining > 0) {
        const int chunk = (remaining < static_cast<int>(sizeof(otaBuf)))
                              ? remaining
                              : static_cast<int>(sizeof(otaBuf));
        const int recv = httpd_req_recv(req, otaBuf, static_cast<size_t>(chunk));
        if (recv <= 0) {
            esp_ota_abort(handle);
            return ESP_FAIL;
        }
        if (esp_ota_write(handle, otaBuf, static_cast<size_t>(recv)) != ESP_OK) {
            esp_ota_abort(handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write failed");
            return ESP_FAIL;
        }
        remaining -= recv;
    }
    if (esp_ota_end(handle) != ESP_OK) { // SHA-256 verify
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "verify failed");
        return ESP_FAIL;
    }
    esp_ota_set_boot_partition(target);
    httpd_resp_sendstr(req, "OK");
    logLine(cfg::log::Level::kInfo, "OTA", "success, restarting");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
    return ESP_OK;
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
    g_webConfig = g_config; // both copies start identical (boot: no tasks yet)
}

// Persist the authoritative (web-edited) config.
void saveConfig(const ConfigModel& c) {
    g_nvs.putBool(kKeyBeeper, c.beeperEnabled);
    g_nvs.putInt16(kKeyDiffThr, c.diffThresholdC100);
    g_nvs.putInt16(kKeyDiffHyst, c.diffHysteresisC100);
    g_nvs.putInt16(kKeyFireThr, c.fireThrC100);
    g_nvs.putInt16(kKeyFireHyst, c.fireHysteresisC100);
    g_nvs.putUint8(kKeyContrast, c.lcdContrastPwm);
    g_nvs.putBool(kKeyEmail, c.emailEnabled);
    g_nvs.putUint8(kKeyGoal, c.windowGoal);
}

// Accumulate window event flags from either core (guarded).
void addWindowFlags(EventFlags f) {
    taskENTER_CRITICAL(&g_mux);
    g_windowFlags = static_cast<EventFlags>(g_windowFlags | f);
    taskEXIT_CRITICAL(&g_mux);
}

// Forward declarations for email body builders (defined after the BLE section).
static const char* buildAlarmBody(const json_api::CurrentStatus& s, bool isFire);
static const char* buildStatusBody(const json_api::CurrentStatus& s);

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
        logLine(cfg::log::Level::kError, "WEB", "serializeCurrent: buffer overflow");
        g_server.send(500, "application/json", R"({"error":"serialize failed"})");
        return;
    }
    g_server.send(200, "application/json", g_jsonCurrent);
}
void handleApiHistory() {
    const uint32_t uptimeS = millis() / 1000UL;
    // Copy the ring buffer under the lock; serialize the (long) loop outside it.
    taskENTER_CRITICAL(&g_mux);
    g_histScratch = g_history;
    taskEXIT_CRITICAL(&g_mux);
    const size_t n =
        json_api::serializeHistory(g_histScratch, uptimeS, g_jsonHistory, sizeof(g_jsonHistory));
    if (n == 0) {
        logLine(cfg::log::Level::kError, "WEB", "serializeHistory: buffer overflow");
        g_server.send(500, "application/json", R"({"error":"serialize failed"})");
        return;
    }
    g_server.send(200, "application/json", g_jsonHistory);
}

// Read a web arg as a long, clamped to [lo, hi]; out-of-range/missing -> fallback.
long argClamped(const char* name, long fallback, long lo, long hi) {
    if (!g_server.hasArg(name)) {
        return fallback;
    }
    const long v = g_server.arg(name).toInt();
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

void handleApiConfig() {
    ConfigModel next   = g_webConfig;
    next.beeperEnabled = argClamped(kKeyBeeper, next.beeperEnabled ? 1 : 0, 0, 1) != 0;
    next.emailEnabled  = argClamped("email", next.emailEnabled ? 1 : 0, 0, 1) != 0;
    next.windowGoal    = static_cast<uint8_t>(argClamped("window_goal", next.windowGoal, 0, 1));
    next.diffThresholdC100 =
        static_cast<int16_t>(argClamped("diff_thr_c100", next.diffThresholdC100, 1, 30000));
    next.diffHysteresisC100 =
        static_cast<int16_t>(argClamped("diff_hyst_c100", next.diffHysteresisC100, 0, 30000));
    next.fireThrC100 =
        static_cast<int16_t>(argClamped("fire_thr_c100", next.fireThrC100, 1, 30000));
    next.fireHysteresisC100 =
        static_cast<int16_t>(argClamped("fire_hyst_c100", next.fireHysteresisC100, 0, 30000));
    next.lcdContrastPwm = static_cast<uint8_t>(argClamped("contrast", next.lcdContrastPwm, 0, 255));
    if (!next.validate()) {
        g_server.send(400, "text/plain", "invalid config");
        return;
    }
    taskENTER_CRITICAL(&g_mux);
    g_webConfig = next; // Core 1 latches this into g_config next cycle
    taskEXIT_CRITICAL(&g_mux);
    saveConfig(next);
    g_pwm.setContrastDuty(next.lcdContrastPwm);
    logLine(cfg::log::Level::kInfo, "WEB", "config updated");
    g_server.send(200, "text/plain", "ok");
}

void handleAction() {
    const String action = g_server.pathArg(0);
    if (action == "test-beep") {
        // g_beep is also written by Core 1; guard the playTone call with g_mux.
        const uint32_t now = millis();
        taskENTER_CRITICAL(&g_mux);
        g_beep.playTone(cfg::beep::kTestToneHz, now);
        taskEXIT_CRITICAL(&g_mux);
    } else if (action == "test-email" || action == "status-email") {
        if (!g_webConfig.emailEnabled) {
            logLine(cfg::log::Level::kWarn, "MAIL", "test email: disabled in config");
            g_server.send(409, "text/plain", "email disabled");
            return;
        }
        if (!g_wifi.isConnected()) {
            logLine(cfg::log::Level::kWarn, "MAIL", "test email: WiFi not connected");
            g_server.send(503, "text/plain", "WiFi not connected");
            return;
        }
        logLine(cfg::log::Level::kInfo, "MAIL", "test email: connecting to SMTP");
        // Manual sends bypass the §7 rate limiter. SMTP can exceed the WDT window,
        // so unsubscribe this (web) task around the blocking send.
        const json_api::CurrentStatus snap = snapshotCopy();
        esp_task_wdt_delete(nullptr);
        const bool ok =
            g_mailer.send(RECIPIENT_EMAIL, "Teplomer: status", buildStatusBody(snap)).isOk();
        if (esp_task_wdt_add(nullptr) != ESP_OK) {
            logLine(cfg::log::Level::kError, "WDT", "wdt_add failed after SMTP (test-email)");
        }
        addWindowFlags(ok ? cfg::flag::kEmailSent : cfg::flag::kEmailFailed);
        logLine(ok ? cfg::log::Level::kInfo : cfg::log::Level::kError, "MAIL",
                ok ? "test email: sent" : "test email: FAILED (check SMTP host/creds)");
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
// Format a temperature into an 8-char LCD row.
// Layout: prefix(1) + %5.1f right-justified(5) + CGRAM °(1) + 'C'(1) = 8 chars.
// '\x01' is CGRAM slot kDegreeGlyph (custom bitmap, ROM-independent). Slot 1
// avoids the null-terminator issue of slot 0 ('\x00'). '\x01C' would parse as
// hex 0x1C, so the 'C' is separated via adjacent string literal concatenation.
static void fmtTempRow(char* buf, size_t bufsz, char prefix, Temperature c100) {
    int n;
    if (c100 == kTempInvalid) {
        n = snprintf(buf, bufsz, "%c  --.-\x01", prefix); // 1 + 2 + 4 + 1 = 8
    } else {
        n = snprintf(buf, bufsz,
                     "%c%5.1f\x01"
                     "C",
                     prefix, c100 / 100.0); // 1+5+1+1 = 8
    }
    if (n < 0) {
        buf[0] = '\0';
    }
}

void renderLcd(const json_api::CurrentStatus& s) {
    // Row 0: inner temperature — always visible.
    char row0[cfg::lcd::kCols + 1];
    fmtTempRow(row0, sizeof(row0), 'I', s.innerC100);
    g_lcd.setCursor(0, 0);
    g_lcd.print(row0);

    // Row 1: alarm/status when active; outer temperature otherwise.
    // Priority: FIRE! > sensor fault > WiFi down > outer temp.
    char row1[cfg::lcd::kCols + 1];
    if (s.fire) {
        if (snprintf(row1, sizeof(row1), "FIRE!   ") < 0) {
            row1[0] = '\0';
        }
    } else if (s.sensorFault) {
        if (snprintf(row1, sizeof(row1), "SENSOR  ") < 0) {
            row1[0] = '\0';
        }
    } else if (!g_wifi.isConnected()) {
        if (snprintf(row1, sizeof(row1), "WiFi DN ") < 0) {
            row1[0] = '\0';
        }
    } else {
        fmtTempRow(row1, sizeof(row1), 'O', s.outerC100);
    }
    g_lcd.setCursor(0, 1);
    g_lcd.print(row1);
}

// ---------------------------------------------------------------------------
// Measurement + alarm cycle (Core 1)
// ---------------------------------------------------------------------------
Temperature readSensor(OneWireBus& bus, AnomalyDetector& det,
                       MovingAverage<cfg::sample::kAvgWindowSamples>& avg, EventFlags& flagsOut) {
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
    // Latch the web-edited config atomically into the Core-1-owned copy that
    // AlarmState reads, so a mid-edit struct is never observed during this cycle.
    taskENTER_CRITICAL(&g_mux);
    g_config = g_webConfig;
    taskEXIT_CRITICAL(&g_mux);

    EventFlags        innerFlags = 0;
    EventFlags        outerFlags = 0;
    const Temperature innerRaw   = readSensor(g_innerBus, g_innerAnomaly, g_innerAvg, innerFlags);
    (void)readSensor(g_outerBus, g_outerAnomaly, g_outerAvg, outerFlags);

    const Temperature innerAvg = g_innerAvg.average();
    const Temperature outerAvg = g_outerAvg.average();
    const EventFlags  flags    = static_cast<EventFlags>(innerFlags | outerFlags);

    // For the diff alarm pass kTempInvalid until both windows are fully populated
    // (FR-02/FR-07: diff is defined over exactly 10 samples; a partial average could
    // produce false trips). Fire and sensor alarms use innerRaw/anomalyFlags, so they
    // are unaffected. Display and JSON use the real averages below.
    const Temperature         innerAvgAlarm = g_innerAvg.isFull() ? innerAvg : kTempInvalid;
    const Temperature         outerAvgAlarm = g_outerAvg.isFull() ? outerAvg : kTempInvalid;
    const MeasurementSnapshot ms{innerRaw, innerAvgAlarm, outerAvgAlarm, flags};
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

    const auto advice = window::advise(innerAvgAlarm, outerAvgAlarm,
                                       static_cast<cfg::window_advisor::Goal>(g_config.windowGoal),
                                       g_config.diffThresholdC100);

    json_api::CurrentStatus s;
    s.innerC100     = innerAvg;
    s.outerC100     = outerAvg;
    s.windowAdvice  = advice;
    s.windowGoal    = g_config.windowGoal;
    s.fire          = g_alarm.isFire();
    s.sensorFault   = g_alarm.isSensorFault();
    s.diffAlarm     = g_alarm.isDiff();
    s.uptimeS       = nowMs / 1000UL;
    s.freeHeap      = g_sys.freeHeap();
    s.minFreeHeap   = g_sys.minFreeHeap();
    s.rssi          = g_wifi.rssi();
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
    if (s.fire)
        f |= cfg::flag::kFire;
    if (s.sensorFault)
        f |= cfg::flag::kSensorOpen;
    if (s.diffAlarm)
        f |= cfg::flag::kDiffExceeded;
    if (s.innerC100 == kTempInvalid)
        f |= cfg::flag::kInnerInvalid;
    if (s.outerC100 == kTempInvalid)
        f |= cfg::flag::kOuterInvalid;

    HistoryRecord rec{};
    rec.t_inner_c100 = g_innerAvg.average();
    rec.t_outer_c100 = g_outerAvg.average();
    taskENTER_CRITICAL(&g_mux);
    rec.flags     = static_cast<uint16_t>(f | g_windowFlags); // fold cross-core window events
    g_windowFlags = 0;                                        // reset for the next window
    g_history.append(rec);
    taskEXIT_CRITICAL(&g_mux);
}

[[noreturn]] void core1Task(void*) {
    esp_task_wdt_add(nullptr);
    uint32_t lastMeasure = 0;
    uint32_t lastHistory = 0;
    uint32_t lastLcd     = 0;
    // readRomId() in setup() reset convStarted_ on both buses, so the prime call
    // below only triggers the DS18B20 conversion (returns kNotReady). We wait the
    // 12-bit conversion time (~750 ms) then call again to read the first real
    // scratchpad — this way the LCD shows actual temperatures from the first frame.
    measurementCycle(millis());
    vTaskDelay(pdMS_TO_TICKS(800));
    measurementCycle(millis()); // reads the conversion; g_snapshot now has real temps
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
            renderLcd(snapshotCopy());
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
    if (s.fire)
        ev |= cfg::flag::kFire;
    if (s.sensorFault)
        ev |= cfg::flag::kSensorOpen;
    if (s.diffAlarm)
        ev |= cfg::flag::kDiffExceeded;
    const auto payload = ble_payload::encode(s.innerC100, s.outerC100, ev, g_bleSeq++);
    g_ble.setPayload(payload.data(), static_cast<uint8_t>(payload.size()));
    g_ble.burst(cfg::ble::kBurstsPerMinute, cfg::ble::kBurstSpacingMs);
}

// ---------------------------------------------------------------------------
// Email body builders (Core 0 only; write into g_emailBody).
// ---------------------------------------------------------------------------
// Format one temperature value; buf must be at least 10 bytes.
static void fmtTemp(char* buf, size_t sz, Temperature c100) {
    if (c100 == kTempInvalid) {
        if (snprintf(buf, sz, "N/A") < 0) {
            buf[0] = '\0';
        }
    } else {
        if (snprintf(buf, sz, "%.1f C", c100 / 100.0) < 0) {
            buf[0] = '\0';
        }
    }
}

static const char* buildAlarmBody(const json_api::CurrentStatus& s, bool isFire) {
    char ip[16];
    g_wifi.getIp(ip, sizeof(ip));
    char tIn[10];
    char tOut[10];
    fmtTemp(tIn, sizeof(tIn), s.innerC100);
    fmtTemp(tOut, sizeof(tOut), s.outerC100);
    const uint32_t upH = s.uptimeS / 3600UL;
    const uint32_t upM = (s.uptimeS % 3600UL) / 60UL;
    const int      n   = snprintf(g_emailBody, sizeof(g_emailBody),
                                  "=== TEPLOMER ALARM ===\r\n\r\n"
                                         "Type:   %s\r\n\r\n"
                                         "Inner:  %s\r\n"
                                         "Outer:  %s\r\n\r\n"
                                         "Uptime: %luh %02lum\r\n"
                                         "IP:     %s\r\n"
                                         "RSSI:   %d dBm\r\n"
                                         "Heap:   %lu B\r\n",
                           isFire ? "FIRE ALARM" : "Sensor fault", tIn, tOut,
                                  static_cast<unsigned long>(upH), static_cast<unsigned long>(upM), ip,
                                  static_cast<int>(s.rssi), static_cast<unsigned long>(s.freeHeap));
    if (n < 0) {
        g_emailBody[0] = '\0';
    }
    return g_emailBody;
}

static const char* buildStatusBody(const json_api::CurrentStatus& s) {
    char ip[16];
    g_wifi.getIp(ip, sizeof(ip));
    char tIn[10];
    char tOut[10];
    fmtTemp(tIn, sizeof(tIn), s.innerC100);
    fmtTemp(tOut, sizeof(tOut), s.outerC100);
    const uint32_t upH = s.uptimeS / 3600UL;
    const uint32_t upM = (s.uptimeS % 3600UL) / 60UL;
    const int      n   = snprintf(g_emailBody, sizeof(g_emailBody),
                                  "=== TEPLOMER STATUS ===\r\n\r\n"
                                         "Inner:  %s\r\n"
                                         "Outer:  %s\r\n"
                                         "Fire:   %s  Sensor: %s  Diff: %s\r\n\r\n"
                                         "Fire thr:  %.1f C / hyst %.1f C\r\n"
                                         "Diff thr:  %.1f C / hyst %.1f C\r\n\r\n"
                                         "Uptime: %luh %02lum\r\n"
                                         "IP:     %s\r\n"
                                         "RSSI:   %d dBm\r\n"
                                         "Heap:   %lu B\r\n"
                                         "Email:  %s  Beeper: %s\r\n",
                                  tIn, tOut, s.fire ? "YES" : "no", s.sensorFault ? "YES" : "no",
                           s.diffAlarm ? "YES" : "no", s.fireThrC100 / 100.0,
                                  s.fireHystC100 / 100.0, s.diffThrC100 / 100.0, s.diffHystC100 / 100.0,
                                  static_cast<unsigned long>(upH), static_cast<unsigned long>(upM), ip,
                                  static_cast<int>(s.rssi), static_cast<unsigned long>(s.freeHeap),
                           s.emailEnabled ? "on" : "off", s.beeperEnabled ? "on" : "off");
    if (n < 0) {
        g_emailBody[0] = '\0';
    }
    return g_emailBody;
}

void checkEmail(const json_api::CurrentStatus& s, uint32_t nowMs) {
    // Only attempt while connected: an offline attempt would just fail, and we
    // must not let it stand in for a real notification (the policy anchors on
    // success only, but skipping offline also avoids a pointless WDT-off window).
    if (!g_wifi.isConnected()) {
        return;
    }
    const bool fireDue =
        g_email.shouldSend(email_policy::Type::kFire, s.fire, s.emailEnabled, nowMs);
    const bool sensorDue =
        g_email.shouldSend(email_policy::Type::kSensorFault, s.sensorFault, s.emailEnabled, nowMs);
    if (!fireDue && !sensorDue) {
        return;
    }
    // Both types have independent rate limiters and independent edges, so send
    // each separately when due (e.g., a sensor detach during a fire triggers both).
    esp_task_wdt_delete(nullptr); // SMTP can exceed the WDT window; unsubscribe for the duration
    if (fireDue) {
        const bool ok =
            g_mailer.send(RECIPIENT_EMAIL, "Teplomer: POZAR", buildAlarmBody(s, true)).isOk();
        if (ok) {
            g_email.markSent(email_policy::Type::kFire, nowMs);
        }
        addWindowFlags(ok ? cfg::flag::kEmailSent : cfg::flag::kEmailFailed);
        logLine(ok ? cfg::log::Level::kInfo : cfg::log::Level::kError, "MAIL",
                ok ? "alarm email sent (fire)" : "alarm email failed (fire)");
    }
    if (sensorDue) {
        const bool ok =
            g_mailer.send(RECIPIENT_EMAIL, "Teplomer: porucha cidla", buildAlarmBody(s, false))
                .isOk();
        if (ok) {
            g_email.markSent(email_policy::Type::kSensorFault, nowMs);
        }
        addWindowFlags(ok ? cfg::flag::kEmailSent : cfg::flag::kEmailFailed);
        logLine(ok ? cfg::log::Level::kInfo : cfg::log::Level::kError, "MAIL",
                ok ? "alarm email sent (sensor)" : "alarm email failed (sensor)");
    }
    if (esp_task_wdt_add(nullptr) != ESP_OK) {
        logLine(cfg::log::Level::kError, "WDT", "wdt_add failed after SMTP (alarm)");
    }
}

[[noreturn]] void core0Task(void*) {
    esp_task_wdt_add(nullptr);
    const uint32_t start       = millis();
    uint32_t       backoffMs   = cfg::net::kReconnectMinMs;
    uint32_t       lastWifiTry = 0;     // 0 = try to connect immediately
    uint32_t       lastBle     = start; // first beacon one period after boot
    uint32_t       lastEmail   = start; // (after Core 1 has a real snapshot)
    bool           mdnsStarted = false;
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
                g_server.begin(); // safe here: WiFi.begin() has initialised the lwIP stack
                g_wifi.startMdns(cfg::net::kMdnsHostname);
                mdnsStarted = true;
                char ip[16];
                g_wifi.getIp(ip, sizeof(ip));
                logLine(cfg::log::Level::kInfo, "WIFI", ip);

                if (g_httpsServer == nullptr) {
                    httpd_ssl_config_t conf   = HTTPD_SSL_CONFIG_DEFAULT();
                    // In ESP-IDF 4.x / Arduino-ESP32 2.x the server's own cert is
                    // named cacert_pem/cacert_len (confusingly); ESP-IDF 5.x renames
                    // it to servercert/servercert_len.
                    conf.cacert_pem           = reinterpret_cast<const uint8_t*>(certs::kCert);
                    conf.cacert_len           = sizeof(certs::kCert);
                    conf.prvtkey_pem          = reinterpret_cast<const uint8_t*>(certs::kKey);
                    conf.prvtkey_len          = sizeof(certs::kKey);
                    conf.port_secure          = cfg::net::kOtaHttpsPort;
                    conf.httpd.stack_size     = 10240; // TLS handshake needs extra stack
                    conf.httpd.recv_wait_timeout = 30; // 30 s to receive firmware
                    conf.httpd.send_wait_timeout = 10;
                    if (httpd_ssl_start(&g_httpsServer, &conf) == ESP_OK) {
                        static const httpd_uri_t kOtaUri = {
                            "/api/ota", HTTP_POST, handleOtaPost, nullptr, false, false, nullptr};
                        httpd_register_uri_handler(g_httpsServer, &kOtaUri);
                        logLine(cfg::log::Level::kInfo, "OTA", "HTTPS server up on :8443");
                    } else {
                        logLine(cfg::log::Level::kError, "OTA", "HTTPS server start failed");
                    }
                }
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

    // Confirm new OTA firmware on the first boot after an OTA update so the
    // bootloader does not roll back to the previous partition.
    {
        const esp_partition_t* running   = esp_ota_get_running_partition();
        esp_ota_img_states_t   ota_state{};
        if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK &&
            ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            esp_ota_mark_app_valid_cancel_rollback();
            logLine(cfg::log::Level::kInfo, "OTA", "new firmware validated OK");
        }
    }

    if (g_sys.resetReason() == ResetReason::kBrownout) {
        logLine(cfg::log::Level::kWarn, "BOOT", "recovered from brownout");
        addWindowFlags(cfg::flag::kBrownoutRecover); // recorded in the first history record (§8.2)
    }

    g_lcd.init();
    // Load a custom ° glyph into CGRAM slot kDegreeGlyph (slot 0 = '\0', unusable
    // in C strings, so we use slot 1). Bitmap: 5×8, small circle at top of cell.
    static const uint8_t kDegreeBitmap[8] = {0x0E, 0x11, 0x11, 0x0E, 0x00, 0x00, 0x00, 0x00};
    g_lcd.createChar(cfg::lcd::kDegreeGlyph, kDegreeBitmap);
    g_pwm.initContrast(cfg::pin::kLcdContrastV0, cfg::ledc::kContrastChannel);
    g_pwm.initBuzzer(cfg::pin::kBuzzer, cfg::ledc::kBuzzerChannel);

    loadConfig();
    g_beep.setEnabled(g_config.beeperEnabled);
    g_pwm.setContrastDuty(g_config.lcdContrastPwm);

    // Read each sensor's ROM ID once for diagnostics.
    const Result<uint64_t> innerRom = g_innerBus.readRomId();
    const Result<uint64_t> outerRom = g_outerBus.readRomId();
    g_snapshot.innerRom             = innerRom.isOk() ? innerRom.value() : 0;
    g_snapshot.outerRom             = outerRom.isOk() ? outerRom.value() : 0;

    g_ble.init(cfg::ble::kDeviceName, cfg::ble::kCompanyId);
    setupBleOtaGatt();

    g_server.on("/", HTTP_GET, handleIndex);
    g_server.on("/app.js", HTTP_GET, handleAppJs);
    g_server.on("/api/current", HTTP_GET, handleApiCurrent);
    g_server.on("/api/history", HTTP_GET, handleApiHistory);
    g_server.on("/api/config", HTTP_POST, handleApiConfig);
    g_server.on(UriBraces("/api/action/{}"), HTTP_POST, handleAction);
    // g_server.begin() is intentionally deferred to core0Task once WiFi connects:
    // WebServer::begin() calls into lwIP (tcpip_send_msg_wait_sem) which asserts if
    // the TCP/IP adapter has not been initialised yet by WiFi.begin().

    g_beep.playTone(cfg::beep::kBootToneHz, millis());

    // Task WDT: 8 s, panic-reset (NFR-02). arduino-esp32 may already have called
    // esp_task_wdt_init() in initArduino() with its own timeout. Deinit first so
    // our call unconditionally applies the correct timeout and panic flag.
    (void)esp_task_wdt_deinit();
    const esp_err_t wdtErr =
        esp_task_wdt_init(cfg::safety::kWdtTimeoutMs / 1000U, cfg::safety::kPanicOnWdt);
    if (wdtErr != ESP_OK) {
        logLine(cfg::log::Level::kWarn, "WDT", "init failed — SDK default in effect");
    }

    // Static stacks and TCBs: no heap allocation at task creation time (NFR-04/07).
    xTaskCreateStaticPinnedToCore(core1Task, "core1", cfg::task::kStackSensor, nullptr,
                                  cfg::task::kPrioSensor, s_core1Stack, &s_core1Tcb,
                                  cfg::task::kCoreApp);
    xTaskCreateStaticPinnedToCore(core0Task, "core0", cfg::task::kStackWeb, nullptr,
                                  cfg::task::kPrioWeb, s_core0Stack, &s_core0Tcb,
                                  cfg::task::kCoreNet);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000)); // all work runs in the pinned tasks
}
