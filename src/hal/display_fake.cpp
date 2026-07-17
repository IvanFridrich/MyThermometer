#include "hal/display.h"

#include <cstdint>

#include "result.h"

// Host fake: records the last frame/brightness and call counts for inspection.

Result<void> Display::init() {
    resetFake();
    return Result<void>::ok();
}

void Display::setBrightness(uint8_t duty) {
    brightness_ = duty;
}

void Display::render(const DisplayFrame& frame) {
    lastFrame_ = frame;
    ++renderCount_;
}

void Display::tick() {
    ++tickCount_;
}

void Display::resetFake() {
    lastFrame_   = DisplayFrame{};
    brightness_  = 0;
    renderCount_ = 0;
    tickCount_   = 0;
}
