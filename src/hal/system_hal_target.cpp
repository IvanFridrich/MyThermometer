#include "hal/system_hal.h"

#include <Arduino.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

// System utilities on ESP32-S3: heap stats, task-WDT feed, restart, reset reason.
// WDT registration/timeout is configured in app setup (esp_task_wdt_init +
// esp_task_wdt_add per task); here we only feed the calling task's WDT.

void SystemHal::restart() {
    ESP.restart();
}

uint32_t SystemHal::freeHeap() const {
    return ESP.getFreeHeap();
}

uint32_t SystemHal::minFreeHeap() const {
    return ESP.getMinFreeHeap();
}

void SystemHal::wdtFeed() {
    esp_task_wdt_reset();
}

ResetReason SystemHal::resetReason() const {
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:
        return ResetReason::kPowerOn;
    case ESP_RST_BROWNOUT:
        return ResetReason::kBrownout;
    case ESP_RST_TASK_WDT:
    case ESP_RST_INT_WDT:
    case ESP_RST_WDT:
        return ResetReason::kWatchdog;
    case ESP_RST_SW:
        return ResetReason::kSoftware;
    case ESP_RST_PANIC:
        return ResetReason::kPanic;
    default:
        return ResetReason::kUnknown;
    }
}
