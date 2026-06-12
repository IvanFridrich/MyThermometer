// Phase 6 will replace this stub with the ESP-IDF WDT / heap / reset driver.
#include "hal/system_hal.h"

void     SystemHal::restart() {}
uint32_t SystemHal::freeHeap() const {
    return 0;
}
uint32_t SystemHal::minFreeHeap() const {
    return 0;
}
void        SystemHal::wdtFeed() {}
ResetReason SystemHal::resetReason() const {
    return ResetReason::kUnknown;
}
