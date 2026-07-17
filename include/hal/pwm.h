#pragma once
#include <cstdint>

#include "result.h"

// HAL: LEDC PWM buzzer channel (variable freq). Display backlight PWM moved to
// the Display HAL. Target: esp_ledc driver. Fake: records last calls.
class Pwm {
  public:
    Result<void> initBuzzer(uint8_t pin, uint8_t channel);

    void tone(uint16_t hz);
    void noTone();

#ifdef NATIVE_BUILD
    // Inspection API
    uint8_t  buzzerPin() const { return buzzerPin_; }
    uint8_t  buzzerChannel() const { return buzzerChannel_; }
    uint16_t lastToneHz() const { return lastHz_; }
    bool     toneActive() const { return toneActive_; }

  private:
    uint8_t  buzzerPin_{0};
    uint8_t  buzzerChannel_{0};
    uint16_t lastHz_{0};
    bool     toneActive_{false};
#endif

#ifndef NATIVE_BUILD
  private:
    uint8_t buzzerChannel_{0}; // LEDC channel bound in initBuzzer()
#endif
};
