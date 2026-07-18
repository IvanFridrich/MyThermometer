#include "beep_engine.h"

#include <cstdint>

#include "Config.h"

namespace beep {

void BeepEngine::playTone(uint16_t freqHz, uint32_t nowMs) {
    if (!enabled_ || (active_ && fireMode_)) {
        return; // disabled, or fire pattern owns the buzzer
    }
    steps_[0]  = Step{freqHz, cfg::beep::kBeepDurationMs};
    stepCount_ = 1;
    startMs_   = nowMs;
    totalMs_   = cfg::beep::kBeepDurationMs;
    repeat_    = false;
    fireMode_  = false;
    active_    = true;
}

void BeepEngine::playSequence3(uint16_t f0, uint16_t f1, uint16_t f2, uint32_t nowMs) {
    if (!enabled_ || (active_ && fireMode_)) {
        return; // disabled, or fire pattern owns the buzzer
    }
    steps_[0]  = Step{f0, cfg::beep::kTriadStepMs};
    steps_[1]  = Step{f1, cfg::beep::kTriadStepMs};
    steps_[2]  = Step{f2, cfg::beep::kTriadStepMs};
    stepCount_ = 3;
    startMs_   = nowMs;
    totalMs_   = 3U * cfg::beep::kTriadStepMs;
    repeat_    = false;
    fireMode_  = false;
    active_    = true;
}

void BeepEngine::playWindowOpen(uint32_t nowMs) {
    playSequence3(cfg::beep::kTriadPrimaHz, cfg::beep::kTriadTertiaHz, cfg::beep::kTriadQuintaHz,
                  nowMs);
}

void BeepEngine::playWindowClose(uint32_t nowMs) {
    playSequence3(cfg::beep::kTriadQuintaHz, cfg::beep::kTriadTertiaHz, cfg::beep::kTriadPrimaHz,
                  nowMs);
}

void BeepEngine::playFire(uint32_t nowMs) {
    playFire(nowMs, cfg::beep::kFireBurstMs);
}

void BeepEngine::playFire(uint32_t nowMs, uint32_t durationMs) {
    if (!enabled_) {
        return;
    }
    steps_[0]  = Step{cfg::beep::kFireToneLowHz, cfg::beep::kFireToneStepMs};
    steps_[1]  = Step{cfg::beep::kFireToneHighHz, cfg::beep::kFireToneStepMs};
    stepCount_ = 2;
    startMs_   = nowMs;
    totalMs_   = durationMs;
    repeat_    = true;
    fireMode_  = true;
    active_    = true;
}

void BeepEngine::stop() {
    active_   = false;
    fireMode_ = false;
}

uint16_t BeepEngine::tick(uint32_t nowMs) {
    if (!enabled_ || !active_) {
        return 0;
    }
    const uint32_t elapsed = nowMs - startMs_; // modular: correct across a wrap
    if (elapsed >= totalMs_) {
        active_   = false;
        fireMode_ = false;
        return 0;
    }

    // Sum of step durations; always > 0 while active_ (set by playTone/playFire).
    uint32_t cyc = 0;
    for (uint8_t i = 0; i < stepCount_; ++i) {
        cyc += steps_[i].durationMs;
    }
    if (cyc == 0U) {
        return 0; // defensive: no playable steps (cannot occur while active_)
    }
    const uint32_t pos = repeat_ ? (elapsed % cyc) : elapsed;

    // Walk all but the last step; pos within the final window falls through.
    uint32_t accum = 0;
    for (uint8_t i = 0; i + 1U < stepCount_; ++i) {
        accum += steps_[i].durationMs;
        if (pos < accum) {
            return steps_[i].freqHz;
        }
    }
    return steps_[stepCount_ - 1U].freqHz;
}

} // namespace beep
