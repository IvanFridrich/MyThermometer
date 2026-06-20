#include "hal/pwm.h"

#include <Arduino.h>

#include "Config.h"

// LEDC PWM driver (arduino-esp32 2.x channel API).
//   Contrast: fixed-frequency, 8-bit duty -> RC low-pass -> LCD V0 (FR-13).
//   Buzzer:   variable-frequency square wave on a passive buzzer; pitch via
//             ledcWriteTone, silence via duty 0. The non-blocking beep/fire
//             pattern engine lives in the app layer (Phase 5), not here.

Result<void> Pwm::initContrast(uint8_t pin, uint8_t channel) {
    contrastChannel_ = channel;
    ledcSetup(channel, cfg::ledc::kContrastFreqHz, cfg::ledc::kContrastResBits);
    ledcAttachPin(pin, channel);
    ledcWrite(channel, cfg::ledc::kContrastDefault);
    return Result<void>::ok();
}

Result<void> Pwm::initBuzzer(uint8_t pin, uint8_t channel) {
    buzzerChannel_ = channel;
    // Initial frequency is a placeholder; tone() sets the real pitch per note.
    ledcSetup(channel, cfg::beep::kTestToneHz, cfg::ledc::kBuzzerResBits);
    ledcAttachPin(pin, channel);
    ledcWrite(channel, 0); // start silent
    return Result<void>::ok();
}

void Pwm::setContrastDuty(uint8_t duty) {
    ledcWrite(contrastChannel_, duty);
}

void Pwm::tone(uint16_t hz) {
    if (hz == 0) {
        noTone(); // 0 Hz must reliably silence; do not rely on ledcWriteTone(0)
        return;
    }
    ledcWriteTone(buzzerChannel_, hz);
}

void Pwm::noTone() {
    ledcWrite(buzzerChannel_, 0);
}
