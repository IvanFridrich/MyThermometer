#include "hal/display.h"

#include <array>

#include <Arduino.h>
#include <LiquidCrystal.h>

#include "Config.h"

// HD44780 2x8 LCD in 4-bit mode via the Arduino LiquidCrystal library.
// RW is tied to GND on the board (write-only). The pin map flows from
// cfg::pin::kLcd* through wiring.h into this constructor.

Display::Display(uint8_t rs, uint8_t en, uint8_t d4, uint8_t d5, uint8_t d6, uint8_t d7)
    : lcd_(rs, en, d4, d5, d6, d7) {}

Result<void> Display::init() {
    lcd_.begin(cfg::lcd::kCols, cfg::lcd::kRows);
    lcd_.clear();
    return Result<void>::ok();
}

void Display::clear() {
    lcd_.clear();
}

void Display::setCursor(uint8_t col, uint8_t row) {
    lcd_.setCursor(col, row);
}

void Display::print(const char* text) {
    if (text != nullptr) {
        lcd_.print(text);
    }
}

void Display::createChar(uint8_t slot, const uint8_t* bitmap8bytes) {
    if (bitmap8bytes == nullptr) {
        return;
    }
    // LiquidCrystal::createChar takes a non-const pointer; copy into a local.
    std::array<uint8_t, 8> glyph{};
    for (uint8_t i = 0; i < glyph.size(); ++i) {
        glyph[i] = bitmap8bytes[i];
    }
    lcd_.createChar(slot, glyph.data());
}
