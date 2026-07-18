#pragma once
#include <array>
#include <cstdint>

// Non-blocking buzzer pattern engine (pwm-contrast-buzzer skill). Pure domain:
// the buzzer task ticks it each cycle against the HAL clock and drives
// Pwm::tone()/noTone() with the returned frequency (0 = silence). A one-shot
// event tone and the repeating fire pattern are just different step sequences,
// so new melodies are easy to add. Never blocks; no delay().
namespace beep {

class BeepEngine {
  public:
    void               setEnabled(bool enabled) { enabled_ = enabled; }
    [[nodiscard]] bool enabled() const { return enabled_; }

    // One-shot single tone for cfg::beep::kBeepDurationMs. Ignored when disabled
    // or while the fire pattern is playing (fire has priority).
    void playTone(uint16_t freqHz, uint32_t nowMs);

    // Window-advice melody: major triad ascending (prima-tercie-kvinta) when
    // the recommendation flips to "open", descending when it flips back to
    // "closed". Same priority rules as playTone (fire owns the buzzer).
    void playWindowOpen(uint32_t nowMs);
    void playWindowClose(uint32_t nowMs);

    // Repeating prima/kvarta fire pattern ("hoří, hoří" two-tone). Always
    // takes over (it is the priority alarm). Ignored when disabled. The
    // duration overload plays a shortened burst (web sound test).
    void playFire(uint32_t nowMs);
    void playFire(uint32_t nowMs, uint32_t durationMs);

    void stop();

    // Frequency to drive the buzzer right now (0 = silent). Expires the pattern.
    [[nodiscard]] uint16_t tick(uint32_t nowMs);

    [[nodiscard]] bool isActive() const { return active_; }

  private:
    struct Step {
        uint16_t freqHz;
        uint16_t durationMs;
    };
    static constexpr uint8_t kMaxSteps = 3;

    // Non-repeating three-tone sequence (window-advice melodies).
    void playSequence3(uint16_t f0, uint16_t f1, uint16_t f2, uint32_t nowMs);

    std::array<Step, kMaxSteps> steps_{};
    uint8_t                     stepCount_{0};
    uint32_t                    startMs_{0};
    uint32_t                    totalMs_{0}; // total active duration of the pattern
    bool                        repeat_{false};
    bool                        fireMode_{false};
    bool                        active_{false};
    bool                        enabled_{true};
};

} // namespace beep
