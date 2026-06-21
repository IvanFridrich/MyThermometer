#include "clock.h"

#include <Arduino.h>

uint32_t Clock::millis() {
    return ::millis();
}
