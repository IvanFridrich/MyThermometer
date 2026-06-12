#pragma once
#include <cstdint>

#include "result.h"

// HAL: LEDC PWM — contrast channel (fixed freq → RC → V0) + buzzer channel (variable freq).
// Target: esp_ledc driver (Phase 3).  Fake: records last calls for test inspection.
class Pwm {
  public:
    Result<void> initContrast(uint8_t pin, uint8_t channel);
    Result<void> initBuzzer(uint8_t pin, uint8_t channel);

    void setContrastDuty(uint8_t duty);
    void tone(uint16_t hz);
    void noTone();

#ifdef NATIVE_BUILD
    // Inspection API
    uint8_t  contrastPin() const { return contrastPin_; }
    uint8_t  contrastChannel() const { return contrastChannel_; }
    uint8_t  buzzerPin() const { return buzzerPin_; }
    uint8_t  buzzerChannel() const { return buzzerChannel_; }
    uint8_t  lastContrastDuty() const { return lastDuty_; }
    uint16_t lastToneHz() const { return lastHz_; }
    bool     toneActive() const { return toneActive_; }

  private:
    uint8_t  contrastPin_{0};
    uint8_t  contrastChannel_{0};
    uint8_t  buzzerPin_{0};
    uint8_t  buzzerChannel_{0};
    uint8_t  lastDuty_{0};
    uint16_t lastHz_{0};
    bool     toneActive_{false};
#endif
};
