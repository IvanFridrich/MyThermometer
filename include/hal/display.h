#pragma once
#include <cstdint>

#include "result.h"
#include "types.h"
#include "window_advice.h"

// HAL: Waveshare 1.9" ST7789 IPS 320x170 (landscape) driven by LVGL v9 +
// LovyanGFX (target) / value recorder (fake). Semantic API: the domain passes
// values, never pixels. All LVGL/LGFX state is file-static in display_target.cpp
// so this header compiles on the host with no library includes.
//
// Threading: init()/render()/tick() are Core-1-only (LVGL is not thread-safe).
// setBrightness() touches only LEDC and is safe from any core (web handler on
// Core 0 calls it).

enum class DisplayStatus : uint8_t { kOuterTemp, kFire, kSensorFault, kWifiDown };

struct DisplayFrame {
    Temperature    innerC100{kTempInvalid}; // centi-degC; kTempInvalid -> "--.-"
    Temperature    outerC100{kTempInvalid};
    DisplayStatus  status{DisplayStatus::kOuterTemp};
    window::Advice advice{window::Advice::kClose}; // window icon (open/closed)

    bool operator==(const DisplayFrame& o) const {
        return innerC100 == o.innerC100 && outerC100 == o.outerC100 && status == o.status &&
               advice == o.advice;
    }
};

class Display {
  public:
    Display() = default; // target reads pins from cfg::pin / cfg::display directly

    Result<void> init();                            // gfx + LVGL + widgets + backlight LEDC
    void         setBrightness(uint8_t duty);       // 0..255 (NVS "contrast" value)
    void         render(const DisplayFrame& frame); // updates labels iff frame changed
    void         tick();                            // lv_timer_handler(); call every loop pass

#ifdef NATIVE_BUILD
  public:
    const DisplayFrame& lastFrame() const { return lastFrame_; }
    uint8_t             lastBrightness() const { return brightness_; }
    uint32_t            renderCount() const { return renderCount_; }
    uint32_t            tickCount() const { return tickCount_; }
    void                resetFake();

  private:
    DisplayFrame lastFrame_{};
    uint8_t      brightness_{0};
    uint32_t     renderCount_{0};
    uint32_t     tickCount_{0};
#endif

#ifndef NATIVE_BUILD
  private:
    DisplayFrame cached_{};          // last rendered frame (change detection)
    bool         firstRender_{true}; // force initial paint
#endif
};
