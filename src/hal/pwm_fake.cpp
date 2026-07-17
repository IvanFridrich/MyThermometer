#include "hal/pwm.h"

#include <cstdint>

#include "result.h"

Result<void> Pwm::initBuzzer(uint8_t pin, uint8_t channel) {
    buzzerPin_     = pin;
    buzzerChannel_ = channel;
    return Result<void>::ok();
}

void Pwm::tone(uint16_t hz) {
    lastHz_     = hz;
    toneActive_ = true;
}

void Pwm::noTone() {
    toneActive_ = false;
}
