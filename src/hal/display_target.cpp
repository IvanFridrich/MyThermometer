// Phase 3 will replace this stub with the LiquidCrystal HD44780 driver.
#include "hal/display.h"

static uint8_t g_rs, g_en, g_d4, g_d5, g_d6, g_d7;
Display::Display(uint8_t rs, uint8_t en, uint8_t d4, uint8_t d5, uint8_t d6, uint8_t d7) {
    g_rs = rs;
    g_en = en;
    g_d4 = d4;
    g_d5 = d5;
    g_d6 = d6;
    g_d7 = d7;
}

Result<void> Display::init() {
    return Result<void>::ok();
}
void Display::clear() {}
void Display::setCursor(uint8_t /*col*/, uint8_t /*row*/) {}
void Display::print(const char* /*text*/) {}
void Display::createChar(uint8_t /*slot*/, const uint8_t* /*bitmap8bytes*/) {}
