#pragma once
// wiring.h — HAL object construction wired to cfg::pin::* and cfg::ledc::*.
// No FreeRTOS tasks here; app/*_task.cpp files hold actual task bodies (Phase 4+).
// This header is included by main.cpp / task files to get a canonical set of globals.

#include "Config.h"
#include "alarm_state.h"
#include "anomaly.h"
#include "clock.h"
#include "config_model.h"
#include "event_log.h"
#include "hal/ble_advertiser.h"
#include "hal/display.h"
#include "hal/http_server.h"
#include "hal/mailer.h"
#include "hal/nvs_store.h"
#include "hal/onewire_bus.h"
#include "hal/pwm.h"
#include "hal/system_hal.h"
#include "hal/wifi_hal.h"
#include "history_buffer.h"
#include "moving_average.h"

// Secrets live in include/secrets.h (gitignored).
// On the native / CI build the file does not exist, but app code is not compiled
// in native env (build_src_filter excludes app/ there), so this is safe.
#ifndef NATIVE_BUILD
#include "secrets.h"
#endif

namespace wiring {

// ---------------------------------------------------------------------------
// Sensors — each DS18B20 on its own bus (FR-03, FR-04)
// ---------------------------------------------------------------------------
inline OneWireBus innerBus() {
    return OneWireBus(cfg::pin::kOneWireInside);
}
inline OneWireBus outerBus() {
    return OneWireBus(cfg::pin::kOneWireOutside);
}

// ---------------------------------------------------------------------------
// Display — HD44780 2×8, 4-bit, pin map from Config.h
// ---------------------------------------------------------------------------
inline Display display() {
    return Display(cfg::pin::kLcdRs, cfg::pin::kLcdEn, cfg::pin::kLcdD4, cfg::pin::kLcdD5,
                   cfg::pin::kLcdD6, cfg::pin::kLcdD7);
}

// ---------------------------------------------------------------------------
// PWM — contrast (LEDC ch1) + buzzer (LEDC ch0)
// ---------------------------------------------------------------------------
inline Pwm pwm() {
    return Pwm{};
}

// ---------------------------------------------------------------------------
// WiFi + HTTP server
// ---------------------------------------------------------------------------
inline WifiHal wifi() {
    return WifiHal{};
}
inline HttpServer httpServer() {
    return HttpServer(cfg::net::kHttpPort);
}

// ---------------------------------------------------------------------------
// BLE advertising beacon
// ---------------------------------------------------------------------------
inline BleAdvertiser bleAdvertiser() {
    return BleAdvertiser{};
}

// ---------------------------------------------------------------------------
// Mailer (SMTP creds from secrets.h)
// ---------------------------------------------------------------------------
#ifndef NATIVE_BUILD
inline Mailer mailer() {
    // Credentials are preprocessor macros from secrets.h (see secrets.h.example).
    return Mailer(SMTP_HOST, cfg::email::kSmtpPort, SMTP_USER, SMTP_PASS, SMTP_USER);
}
#endif

// ---------------------------------------------------------------------------
// NVS store
// ---------------------------------------------------------------------------
inline NvsStore nvsStore() {
    return NvsStore("thermo");
}

// ---------------------------------------------------------------------------
// Domain objects
// ---------------------------------------------------------------------------
inline Clock clock() {
    return Clock{};
}
inline EventLog eventLog() {
    return EventLog{};
}
inline HistoryBuffer history() {
    return HistoryBuffer{};
}
inline SystemHal sysHal() {
    return SystemHal{};
}

using Avg10 = MovingAverage<cfg::sample::kAvgWindowSamples>;
inline Avg10 innerAvg() {
    return Avg10{};
}
inline Avg10 outerAvg() {
    return Avg10{};
}

inline AnomalyDetector innerAnomalyDetector() {
    return AnomalyDetector{};
}
inline AnomalyDetector outerAnomalyDetector() {
    return AnomalyDetector{};
}

} // namespace wiring
