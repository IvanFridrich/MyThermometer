// Phase 3 will replace this stub with the ESP-IDF LEDC driver.
#include "hal/pwm.h"

Result<void> Pwm::initContrast(uint8_t /*pin*/, uint8_t /*channel*/) {
    return Result<void>::ok();
}
Result<void> Pwm::initBuzzer(uint8_t /*pin*/, uint8_t /*channel*/) {
    return Result<void>::ok();
}
void Pwm::setContrastDuty(uint8_t /*duty*/) {}
void Pwm::tone(uint16_t /*hz*/) {}
void Pwm::noTone() {}
