#include "hal/system_hal.h"

void SystemHal::restart() {
    ++restarts_;
}
uint32_t SystemHal::freeHeap() const {
    return freeHeap_;
}
uint32_t SystemHal::minFreeHeap() const {
    return minFreeHeap_;
}
void SystemHal::wdtFeed() {
    ++wdtFeeds_;
}
ResetReason SystemHal::resetReason() const {
    return resetReason_;
}
