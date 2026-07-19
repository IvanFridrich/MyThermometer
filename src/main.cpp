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
#include <cstring> // memcpy in OtaDataCb::onWrite
#include <ctime>   // time()/localtime_r for NTP time-of-day
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
#include "quiet_hours.h"
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

#include "certs.h"
#include <NimBLEDevice.h>
#include <esp_https_server.h>
#include <esp_ota_ops.h>

// ---------------------------------------------------------------------------
// HAL + domain instances (constructed once; live for the program lifetime)
// ---------------------------------------------------------------------------
namespace {

OneWireBus    g_innerBus(cfg::pin::kOneWireInside);
OneWireBus    g_outerBus(cfg::pin::kOneWireOutside);
Display       g_display; // ST7789 + LVGL; pins from cfg::pin (target reads directly)
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
char g_emailBody[cfg::email::kBodyBufSize]; // written only by the mail task
// Copy of the ring buffer taken under g_mux so /api/history serializes outside the
// critical section (the serialize loop is too long to hold the spinlock).
HistoryBuffer g_histScratch;

// Event flags accumulated during the current 10-min history window from BOTH cores
// (email sent/failed, brownout-recover). Guarded by g_mux; folded into each record.
EventFlags g_windowFlags = 0;

uint8_t g_bleSeq = 0;

// Two-state window recommendation (advice == kOpen), Core-1-owned. Drives the
// open/close melody on the flip; boot state "closed" so no melody at startup.
bool g_windowOpenPrev = false;

httpd_handle_t g_httpsServer{nullptr};

// Static task stacks + TCBs (NFR-04: no heap in steady state; NFR-07).
// StackType_t is uint8_t on Xtensa/ESP32-S3, so array sizes equal byte counts.
StaticTask_t s_core0Tcb;
StaticTask_t s_core1Tcb;
StackType_t  s_core0Stack[cfg::task::kStackWeb];
StackType_t  s_core1Stack[cfg::task::kStackSensor];

// ---------------------------------------------------------------------------
// Mail task (Core 0, §8.5): blocking SMTP lives in its own task so the web
// task stays responsive during a 5-15 s send (buzzer/actions keep working).
// Jobs carry a snapshot copy; the task is deliberately NOT WDT-registered —
// the blocking send is bounded by the mailer's TCP timeout instead.
// ---------------------------------------------------------------------------
enum class MailJob : uint8_t { kStatus, kFireAlarm, kSensorAlarm };
struct MailReq {
    MailJob                 job;
    json_api::CurrentStatus snap;
};
static StaticTask_t  s_mailTcb;
static StackType_t   s_mailStack[cfg::task::kStackMail];
static uint8_t       s_mailQueueStorage[cfg::email::kQueueDepth * sizeof(MailReq)];
static StaticQueue_t s_mailQueueTcb;
static QueueHandle_t s_mailQueue{nullptr};

// Alarm mail queued or in flight — stops checkEmail from re-enqueueing every
// cycle while the policy still reports "due" (it anchors on success only).
// Guarded by g_mux (set on the Core 0 web task, cleared by the mail task).
bool g_mailFirePending   = false;
bool g_mailSensorPending = false;

// NVS keys (§6.4).
constexpr char kKeyBeeper[]    = "beeper";
constexpr char kKeyDiffThr[]   = "diff_thr";
constexpr char kKeyDiffHyst[]  = "diff_hyst";
constexpr char kKeyFireThr[]   = "fire_thr";
constexpr char kKeyFireHyst[]  = "fire_hyst";
constexpr char kKeyContrast[]  = "contrast";
constexpr char kKeyEmail[]     = "email";
constexpr char kKeyGoal[]      = "win_goal";
constexpr char kKeyQuietFrom[] = "quiet_from";
constexpr char kKeyQuietTo[]   = "quiet_to";

void logLine(cfg::log::Level level, const char* module, const char* msg) {
    const uint32_t up = millis();
    g_log.append(level, module, msg, up);
    Serial.printf("[+%lu][%d][%s] %s\n", static_cast<unsigned long>(up), static_cast<int>(level),
                  module, msg);
}

// Local time-of-day in minutes (0..1439), or -1 if NTP has not synced yet.
// configTzTime() applies the Czech TZ, so localtime_r already accounts for DST.
int16_t localTimeOfDayMin() {
    const time_t now = time(nullptr);
    if (static_cast<uint32_t>(now) < cfg::net::kMinValidEpoch) {
        return -1; // clock not set yet
    }
    tm tmLocal{};
    localtime_r(&now, &tmLocal);
    return static_cast<int16_t>(tmLocal.tm_hour * 60 + tmLocal.tm_min);
}

// ---------------------------------------------------------------------------
// BLE GATT OTA service
// Protocol: CTRL write [0x01, size LE u32] → DATA writes (chunks) →
//           CTRL write [0x02] → STATUS notify [0x00] → restart.
//           CTRL write [0x03] aborts at any point.
//
// Architecture: callbacks only update shared state and enqueue sentinels.
// The dedicated static writer task (Core 1, priority 7) owns ALL esp_ota_*
// calls, preventing flash I/O from stalling the NimBLE host task (Core 0).
// ---------------------------------------------------------------------------

// Queue item: data chunk or sentinel (doStart / doCommit / doAbort).
struct OtaChunk {
    uint8_t  data[cfg::ota::kMaxChunkBytes];
    uint16_t len;
    bool     doStart;  // sentinel: begin new session; len/data unused
    bool     doCommit; // sentinel: verify + restart; len/data unused
    bool     doAbort;  // sentinel: abort current session; len/data unused
};

struct BleOtaCtx {
    esp_ota_handle_t       handle{};
    const esp_partition_t* target{nullptr};
    uint32_t               expected{0};
    uint32_t               received{0};
    bool                   active{false}; // true only while writer has a valid handle
};
static BleOtaCtx             s_bleOta;
static NimBLECharacteristic* s_bleOtaStatus{nullptr};

// Static queue + writer task storage (NFR-04: no heap in steady state).
static uint8_t       s_otaQueueStorage[cfg::ota::kQueueDepth * sizeof(OtaChunk)];
static StaticQueue_t s_otaQueueTcb;
static QueueHandle_t s_otaQueue{nullptr};

static StackType_t  s_otaWriterStack[cfg::ota::kWriterStackBytes];
static StaticTask_t s_otaWriterTcb;
static TaskHandle_t s_otaWriterTask{nullptr};

static void bleOtaNotify(uint8_t code) {
    if (s_bleOtaStatus != nullptr) {
        s_bleOtaStatus->notify(&code, 1);
    }
}

// Sole owner of esp_ota_begin / esp_ota_write / esp_ota_end / esp_ota_abort.
// Callbacks on Core 0 enqueue into s_otaQueue; this task dequeues on Core 1.
static void otaWriterTask(void*) {
    OtaChunk chunk;
    for (;;) {
        if (xQueueReceive(s_otaQueue, &chunk, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (chunk.doStart) {
            if (s_bleOta.active) {
                esp_ota_abort(s_bleOta.handle);
                s_bleOta.active = false;
                logLine(cfg::log::Level::kWarn, "BLE-OTA",
                        "previous session not committed — aborted");
            }
            // OTA_WITH_SEQUENTIAL_WRITES: defers sector erase to write time;
            // avoids blocking the entire partition upfront (several seconds).
            if (esp_ota_begin(s_bleOta.target, OTA_WITH_SEQUENTIAL_WRITES, &s_bleOta.handle) !=
                ESP_OK) {
                logLine(cfg::log::Level::kError, "BLE-OTA", "begin failed");
                bleOtaNotify(0xFFU);
                continue;
            }
            s_bleOta.received = 0;
            s_bleOta.active   = true;
            logLine(cfg::log::Level::kInfo, "BLE-OTA", "started");
        } else if (chunk.doAbort) {
            if (s_bleOta.active) {
                esp_ota_abort(s_bleOta.handle);
                s_bleOta.active = false;
                logLine(cfg::log::Level::kInfo, "BLE-OTA", "aborted");
            }
            // Drain queue so next session starts with an empty queue.
            OtaChunk discard;
            while (xQueueReceive(s_otaQueue, &discard, 0) == pdTRUE) {
            }
        } else if (chunk.doCommit) {
            if (!s_bleOta.active) {
                bleOtaNotify(0xFFU);
                continue;
            }
            if (s_bleOta.received != s_bleOta.expected) {
                logLine(cfg::log::Level::kError, "BLE-OTA", "commit: size mismatch");
                esp_ota_abort(s_bleOta.handle);
                s_bleOta.active = false;
                bleOtaNotify(0xFFU);
                continue;
            }
            if (esp_ota_end(s_bleOta.handle) != ESP_OK) {
                logLine(cfg::log::Level::kError, "BLE-OTA", "commit: verify failed");
                s_bleOta.active = false;
                bleOtaNotify(0xFFU);
                continue;
            }
            if (esp_ota_set_boot_partition(s_bleOta.target) != ESP_OK) {
                logLine(cfg::log::Level::kError, "BLE-OTA", "commit: set boot partition failed");
                s_bleOta.active = false;
                bleOtaNotify(0xFFU);
                continue;
            }
            s_bleOta.active = false;
            logLine(cfg::log::Level::kInfo, "BLE-OTA", "success, restarting");
            bleOtaNotify(0x00U);
            // 1500 ms: long enough for STATUS notify to reach the client before
            // the BLE stack goes down with esp_restart().
            vTaskDelay(pdMS_TO_TICKS(cfg::ota::kNotifyDelayMs));
            esp_restart();
        } else if (s_bleOta.active) {
            // Data chunk.
            if (esp_ota_write(s_bleOta.handle, chunk.data, static_cast<size_t>(chunk.len)) !=
                ESP_OK) {
                logLine(cfg::log::Level::kError, "BLE-OTA", "write failed");
                esp_ota_abort(s_bleOta.handle);
                s_bleOta.active = false;
                bleOtaNotify(0xFFU);
                OtaChunk discard;
                while (xQueueReceive(s_otaQueue, &discard, 0) == pdTRUE) {
                }
            } else {
                s_bleOta.received += static_cast<uint32_t>(chunk.len);
            }
        }
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
            // CTRL_START: set target + expected size, drain queue, send start sentinel.
            s_bleOta.target = esp_ota_get_next_update_partition(nullptr);
            if (s_bleOta.target == nullptr) {
                logLine(cfg::log::Level::kError, "BLE-OTA", "no OTA partition");
                bleOtaNotify(0xFFU);
                return;
            }
            s_bleOta.expected = static_cast<uint32_t>(d[1]) | (static_cast<uint32_t>(d[2]) << 8U) |
                                (static_cast<uint32_t>(d[3]) << 16U) |
                                (static_cast<uint32_t>(d[4]) << 24U);
            // Drain stale queue items from any previous failed session.
            OtaChunk discard;
            while (xQueueReceive(s_otaQueue, &discard, 0) == pdTRUE) {
            }
            OtaChunk start{};
            start.doStart = true;
            xQueueSend(s_otaQueue, &start, 0);
            // onWrite returns immediately — NimBLE sends ATT Write Response.
        } else if (d[0] == 0x02U) {
            // CTRL_COMMIT: enqueue sentinel; writer does esp_ota_end + restart.
            if (!s_bleOta.active) {
                bleOtaNotify(0xFFU);
                return;
            }
            OtaChunk commit{};
            commit.doCommit = true;
            xQueueSend(s_otaQueue, &commit, 0);
            // onWrite returns immediately — NimBLE sends ATT Write Response.
        } else if (d[0] == 0x03U) {
            // CTRL_ABORT
            OtaChunk abort{};
            abort.doAbort = true;
            xQueueSend(s_otaQueue, &abort, 0);
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
        const uint16_t copyLen   = (len <= cfg::ota::kMaxChunkBytes) ? static_cast<uint16_t>(len)
                                                                     : cfg::ota::kMaxChunkBytes;
        if (copyLen == 0) {
            return;
        }
        OtaChunk chunk{};
        chunk.len = copyLen;
        memcpy(chunk.data, d, copyLen);
        if (xQueueSend(s_otaQueue, &chunk, 0) != pdTRUE) {
            // Queue full — should not occur with response=True flow control.
            logLine(cfg::log::Level::kError, "BLE-OTA", "queue full — aborting");
            s_bleOta.active = false;
            OtaChunk abort{};
            abort.doAbort = true;
            xQueueSend(s_otaQueue, &abort, portMAX_DELAY);
        }
    }
};

static OtaCtrlCb s_otaCtrlCb;
static OtaDataCb s_otaDataCb;

struct OtaServerCb final : NimBLEServerCallbacks {
    void onDisconnect(NimBLEServer* /*srv*/, NimBLEConnInfo& /*info*/, int /*reason*/) override {
        // Enqueue abort so writer task cleans up if a session was in progress.
        // No esp_ota_* calls here; writer task is the sole owner of OTA state.
        OtaChunk abort{};
        abort.doAbort = true;
        xQueueSend(s_otaQueue, &abort, 0);
    }
};
static OtaServerCb s_otaServerCb;

void setupBleOtaGatt() {
    s_otaQueue = xQueueCreateStatic(cfg::ota::kQueueDepth, sizeof(OtaChunk), s_otaQueueStorage,
                                    &s_otaQueueTcb);
    // Static writer task on Core 1 at priority 7 (above all sensing tasks).
    // Core 1 avoids competing with NimBLE (Core 0) for CPU time during flash writes.
    s_otaWriterTask = xTaskCreateStaticPinnedToCore(
        otaWriterTask, "ota_writer", cfg::ota::kWriterStackBytes, nullptr,
        cfg::ota::kWriterPriority, s_otaWriterStack, &s_otaWriterTcb, cfg::task::kCoreApp);

    NimBLEServer* srv = NimBLEDevice::createServer();
    srv->setCallbacks(&s_otaServerCb);
    srv->advertiseOnDisconnect(true); // re-advertise after disconnect so device stays visible

    NimBLEService* svc = srv->createService(cfg::ble::kOtaSvcUuid);

    NimBLECharacteristic* ctrl =
        svc->createCharacteristic(cfg::ble::kOtaCtrlUuid, NIMBLE_PROPERTY::WRITE);
    ctrl->setCallbacks(&s_otaCtrlCb);

    NimBLECharacteristic* data = svc->createCharacteristic(
        cfg::ble::kOtaDataUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    data->setCallbacks(&s_otaDataCb);

    s_bleOtaStatus = svc->createCharacteristic(cfg::ble::kOtaStatusUuid, NIMBLE_PROPERTY::NOTIFY);

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
        const int recv  = httpd_req_recv(req, otaBuf, static_cast<size_t>(chunk));
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
    g_config.quietFromMin       = g_nvs.getInt16(kKeyQuietFrom, g_config.quietFromMin).value();
    g_config.quietToMin         = g_nvs.getInt16(kKeyQuietTo, g_config.quietToMin).value();
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
    g_nvs.putInt16(kKeyQuietFrom, c.quietFromMin);
    g_nvs.putInt16(kKeyQuietTo, c.quietToMin);
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
    next.quietFromMin =
        static_cast<int16_t>(argClamped("quiet_from_min", next.quietFromMin, 0, 1439));
    next.quietToMin = static_cast<int16_t>(argClamped("quiet_to_min", next.quietToMin, 0, 1439));
    if (!next.validate()) {
        g_server.send(400, "text/plain", "invalid config");
        return;
    }
    taskENTER_CRITICAL(&g_mux);
    g_webConfig = next; // Core 1 latches this into g_config next cycle
    taskEXIT_CRITICAL(&g_mux);
    saveConfig(next);
    // Core 0 caller; setBrightness touches only LEDC — safe cross-core.
    g_display.setBrightness(next.lcdContrastPwm);
    logLine(cfg::log::Level::kInfo, "WEB", "config updated");
    g_server.send(200, "text/plain", "ok");
}

void handleAction() {
    const String action = g_server.pathArg(0);
    if (action == "test-beep" || action == "test-open" || action == "test-close" ||
        action == "test-fire") {
        // g_beep is also written by Core 1; guard the play* call with g_mux.
        const uint32_t now = millis();
        taskENTER_CRITICAL(&g_mux);
        if (action == "test-open") {
            g_beep.playWindowOpen(now);
        } else if (action == "test-close") {
            g_beep.playWindowClose(now);
        } else if (action == "test-fire") {
            g_beep.playFire(now, cfg::beep::kFireTestMs); // short burst, not the full minute
        } else {
            g_beep.playTone(cfg::beep::kTestToneHz, now);
        }
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
        // Hand off to the mail task and respond immediately: a blocking SMTP
        // send here (5-15 s) froze the web task — no UI feedback, no other
        // actions (e.g. buzzer tests) until it finished. Manual sends bypass
        // the §7 rate limiter; the result lands in the event log.
        MailReq req;
        req.job  = MailJob::kStatus;
        req.snap = snapshotCopy();
        if (xQueueSend(s_mailQueue, &req, 0) != pdTRUE) {
            g_server.send(503, "text/plain", "mail queue full");
            return;
        }
        logLine(cfg::log::Level::kInfo, "MAIL", "status email queued");
        g_server.send(200, "text/plain", "queued");
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
// Display rendering (Core 1)
// ---------------------------------------------------------------------------
void renderDisplay(const json_api::CurrentStatus& s) {
    DisplayFrame f;
    f.innerC100 = s.innerC100;
    f.outerC100 = s.outerC100;
    f.advice    = s.windowAdvice; // window icon: open / closed / dimmed (no change)
    // Priority: FIRE! > sensor fault > WiFi down > outer temp (same as HD44780).
    if (s.fire) {
        f.status = DisplayStatus::kFire;
    } else if (s.sensorFault) {
        f.status = DisplayStatus::kSensorFault;
    } else if (!g_wifi.isConnected()) {
        f.status = DisplayStatus::kWifiDown;
    } else {
        f.status = DisplayStatus::kOuterTemp;
    }
    g_display.render(f);
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
    }
    if (g_alarm.isFire() && !g_beep.isActive()) {
        g_beep.playFire(nowMs); // keep sounding while the fire condition holds (FR-08)
    }

    const auto advice = window::advise(innerAvgAlarm, outerAvgAlarm,
                                       static_cast<cfg::window_advisor::Goal>(g_config.windowGoal),
                                       g_config.diffThresholdC100);

    // Local time-of-day in minutes, or -1 if NTP has not synced yet.
    const int16_t todMin = localTimeOfDayMin();

    // Window-advice melody on the two-state flip (same boundary as the display
    // icon): ascending triad when it becomes "open", descending when it goes
    // back to "closed". Blocked by an active fire pattern (fire owns the buzzer)
    // and, during quiet hours, suppressed entirely. The flip state is still
    // tracked so a change during the quiet window does not replay when it ends.
    const bool windowOpen = advice == window::Advice::kOpen;
    if (windowOpen != g_windowOpenPrev) {
        g_windowOpenPrev = windowOpen;
        const bool quiet =
            todMin >= 0 && quiet::inQuietWindow(static_cast<uint16_t>(todMin),
                                                static_cast<uint16_t>(g_config.quietFromMin),
                                                static_cast<uint16_t>(g_config.quietToMin));
        if (!quiet) {
            if (windowOpen) {
                g_beep.playWindowOpen(nowMs);
            } else {
                g_beep.playWindowClose(nowMs);
            }
        }
    }

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
    s.quietFromMin  = g_config.quietFromMin;
    s.quietToMin    = g_config.quietToMin;
    s.todMin        = todMin;
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
        if (now - lastLcd >= cfg::display::kRefreshMs) {
            lastLcd = now;
            renderDisplay(snapshotCopy());
        }
        const uint16_t hz = g_beep.tick(now);
        if (hz != 0) {
            g_pwm.tone(hz);
        } else {
            g_pwm.noTone();
        }
        g_display.tick(); // lv_timer_handler — LVGL runs exclusively on this core
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
// Format one temperature value; buf must be at least 14 bytes (UTF-8 °C + sign).
static void fmtTemp(char* buf, size_t sz, Temperature c100) {
    if (c100 == kTempInvalid) {
        if (snprintf(buf, sz, "neznámá") < 0) {
            buf[0] = '\0';
        }
    } else {
        if (snprintf(buf, sz, "%.1f °C", c100 / 100.0) < 0) {
            buf[0] = '\0';
        }
    }
}

static const char* buildAlarmBody(const json_api::CurrentStatus& s, bool isFire) {
    char ip[16];
    g_wifi.getIp(ip, sizeof(ip));
    char tIn[14];
    char tOut[14];
    fmtTemp(tIn, sizeof(tIn), s.innerC100);
    fmtTemp(tOut, sizeof(tOut), s.outerC100);
    const uint32_t upH = s.uptimeS / 3600UL;
    const uint32_t upM = (s.uptimeS % 3600UL) / 60UL;
    const int      n   = snprintf(g_emailBody, sizeof(g_emailBody),
                                  "=== TEPLOMĚR — ALARM ===\r\n\r\n"
                                         "Typ:       %s\r\n\r\n"
                                         "Uvnitř:    %s\r\n"
                                         "Venku:     %s\r\n\r\n"
                                         "Doba běhu: %lu h %02lu min\r\n"
                                         "IP:        %s\r\n"
                                         "Signál:    %d dBm\r\n"
                                         "Volná RAM: %lu B\r\n",
                           isFire ? "POŽÁR!" : "Porucha čidla", tIn, tOut,
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
    char tIn[14];
    char tOut[14];
    fmtTemp(tIn, sizeof(tIn), s.innerC100);
    fmtTemp(tOut, sizeof(tOut), s.outerC100);
    const uint32_t upH = s.uptimeS / 3600UL;
    const uint32_t upM = (s.uptimeS % 3600UL) / 60UL;
    const int      n   = snprintf(
        g_emailBody, sizeof(g_emailBody),
        "=== TEPLOMĚR — STAV ===\r\n\r\n"
               "Uvnitř:  %s\r\n"
               "Venku:   %s\r\n"
               "Požár: %s  Čidlo: %s  Rozdíl: %s\r\n\r\n"
               "Práh požáru:  %.1f °C / hystereze %.1f °C\r\n"
               "Práh rozdílu: %.1f °C / hystereze %.1f °C\r\n\r\n"
               "Doba běhu: %lu h %02lu min\r\n"
               "IP:        %s\r\n"
               "Signál:    %d dBm\r\n"
               "Volná RAM: %lu B\r\n"
               "E-mail: %s  Bzučák: %s\r\n",
        tIn, tOut, s.fire ? "ANO" : "ne", s.sensorFault ? "ANO" : "ne", s.diffAlarm ? "ANO" : "ne",
        s.fireThrC100 / 100.0, s.fireHystC100 / 100.0, s.diffThrC100 / 100.0,
        s.diffHystC100 / 100.0, static_cast<unsigned long>(upH), static_cast<unsigned long>(upM),
        ip, static_cast<int>(s.rssi), static_cast<unsigned long>(s.freeHeap),
        s.emailEnabled ? "zapnuto" : "vypnuto", s.beeperEnabled ? "zapnuto" : "vypnuto");
    if (n < 0) {
        g_emailBody[0] = '\0';
    }
    return g_emailBody;
}

void checkEmail(const json_api::CurrentStatus& s, uint32_t nowMs) {
    // Only queue while connected: an offline attempt would just fail, and the
    // policy anchors on success only.
    if (!g_wifi.isConnected()) {
        return;
    }
    // The policy runs every cycle (it tracks edges); the pending flags only
    // gate enqueueing so a slow or failing send is not queued again each cycle.
    // g_email + pending flags are shared with the mail task -> g_mux.
    taskENTER_CRITICAL(&g_mux);
    const bool fireDue =
        g_email.shouldSend(email_policy::Type::kFire, s.fire, s.emailEnabled, nowMs) &&
        !g_mailFirePending;
    if (fireDue) {
        g_mailFirePending = true;
    }
    const bool sensorDue = g_email.shouldSend(email_policy::Type::kSensorFault, s.sensorFault,
                                              s.emailEnabled, nowMs) &&
                           !g_mailSensorPending;
    if (sensorDue) {
        g_mailSensorPending = true;
    }
    taskEXIT_CRITICAL(&g_mux);
    // Both types have independent rate limiters and independent edges, so queue
    // each separately when due (e.g., a sensor detach during a fire triggers both).
    if (fireDue) {
        MailReq req{MailJob::kFireAlarm, s};
        if (xQueueSend(s_mailQueue, &req, 0) != pdTRUE) {
            taskENTER_CRITICAL(&g_mux);
            g_mailFirePending = false; // queue full — retry next cycle
            taskEXIT_CRITICAL(&g_mux);
        }
    }
    if (sensorDue) {
        MailReq req{MailJob::kSensorAlarm, s};
        if (xQueueSend(s_mailQueue, &req, 0) != pdTRUE) {
            taskENTER_CRITICAL(&g_mux);
            g_mailSensorPending = false; // queue full — retry next cycle
            taskEXIT_CRITICAL(&g_mux);
        }
    }
}

// Sole owner of g_mailer and g_emailBody. Runs the blocking SMTP sends so the
// web task never stalls. Not WDT-registered: the send duration is bounded by
// the mailer's TCP timeout (cfg::email::kSendTimeoutMs), not by task health.
void mailTask(void*) {
    MailReq req;
    for (;;) {
        if (xQueueReceive(s_mailQueue, &req, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        bool ok = false;
        switch (req.job) {
        case MailJob::kStatus:
            ok = g_mailer.send(RECIPIENT_EMAIL, "Teploměr: stav", buildStatusBody(req.snap)).isOk();
            logLine(ok ? cfg::log::Level::kInfo : cfg::log::Level::kError, "MAIL",
                    ok ? "status email sent" : "status email FAILED (check SMTP host/creds)");
            break;
        case MailJob::kFireAlarm:
            ok = g_mailer.send(RECIPIENT_EMAIL, "Teploměr: POŽÁR!", buildAlarmBody(req.snap, true))
                     .isOk();
            taskENTER_CRITICAL(&g_mux);
            if (ok) {
                g_email.markSent(email_policy::Type::kFire, millis());
            }
            g_mailFirePending = false;
            taskEXIT_CRITICAL(&g_mux);
            logLine(ok ? cfg::log::Level::kInfo : cfg::log::Level::kError, "MAIL",
                    ok ? "alarm email sent (fire)" : "alarm email failed (fire)");
            break;
        case MailJob::kSensorAlarm:
            ok = g_mailer
                     .send(RECIPIENT_EMAIL, "Teploměr: porucha čidla",
                           buildAlarmBody(req.snap, false))
                     .isOk();
            taskENTER_CRITICAL(&g_mux);
            if (ok) {
                g_email.markSent(email_policy::Type::kSensorFault, millis());
            }
            g_mailSensorPending = false;
            taskEXIT_CRITICAL(&g_mux);
            logLine(ok ? cfg::log::Level::kInfo : cfg::log::Level::kError, "MAIL",
                    ok ? "alarm email sent (sensor)" : "alarm email failed (sensor)");
            break;
        }
        addWindowFlags(ok ? cfg::flag::kEmailSent : cfg::flag::kEmailFailed);
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

                // Start NTP with the Czech TZ (POSIX rule carries DST). SNTP keeps
                // resyncing on its own; re-calling on reconnect is harmless.
                configTzTime(cfg::net::kTimezone, cfg::net::kNtpServer);

                if (g_httpsServer == nullptr) {
                    httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();
                    // In ESP-IDF 4.x / Arduino-ESP32 2.x the server's own cert is
                    // named cacert_pem/cacert_len (confusingly); ESP-IDF 5.x renames
                    // it to servercert/servercert_len.
                    conf.cacert_pem              = reinterpret_cast<const uint8_t*>(certs::kCert);
                    conf.cacert_len              = sizeof(certs::kCert);
                    conf.prvtkey_pem             = reinterpret_cast<const uint8_t*>(certs::kKey);
                    conf.prvtkey_len             = sizeof(certs::kKey);
                    conf.port_secure             = cfg::net::kOtaHttpsPort;
                    conf.httpd.stack_size        = 10240; // TLS handshake needs extra stack
                    conf.httpd.recv_wait_timeout = 30;    // 30 s to receive firmware
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
            // Skip beacon while a GATT client is connected (OTA in progress).
            NimBLEServer* bleSrv = NimBLEDevice::getServer();
            if (bleSrv == nullptr || bleSrv->getConnectedCount() == 0) {
                sendBleBeacon(s);
            }
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
        const esp_partition_t* running = esp_ota_get_running_partition();
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

    if (!g_display.init().isOk()) {
        logLine(cfg::log::Level::kError, "DISP", "display init failed");
    }
    g_pwm.initBuzzer(cfg::pin::kBuzzer, cfg::ledc::kBuzzerChannel);

    loadConfig();
    g_beep.setEnabled(g_config.beeperEnabled);
    g_display.setBrightness(g_config.lcdContrastPwm);

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
    s_mailQueue = xQueueCreateStatic(cfg::email::kQueueDepth, sizeof(MailReq), s_mailQueueStorage,
                                     &s_mailQueueTcb);
    xTaskCreateStaticPinnedToCore(mailTask, "mail", cfg::task::kStackMail, nullptr,
                                  cfg::task::kPrioMail, s_mailStack, &s_mailTcb,
                                  cfg::task::kCoreNet);
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
