#include "hal/display.h"

#include <cstdint>
#include <cstring>

#include "result.h"

Display::Display(uint8_t /*rs*/, uint8_t /*en*/, uint8_t /*d4*/, uint8_t /*d5*/, uint8_t /*d6*/,
                 uint8_t /*d7*/) {}

Result<void> Display::init() {
    resetFake();
    return Result<void>::ok();
}

void Display::clear() {
    std::memset(rows_[0], ' ', 8);
    std::memset(rows_[1], ' ', 8);
    rows_[0][8] = '\0';
    rows_[1][8] = '\0';
    curCol_     = 0;
    curRow_     = 0;
}

void Display::setCursor(uint8_t col, uint8_t row) {
    curCol_ = (col < 8U) ? col : 7U;
    curRow_ = (row < 2U) ? row : 1U;
}

void Display::print(const char* text) {
    if (text == nullptr || curRow_ >= 2U) {
        return;
    }
    const char* src = text;
    while (*src != '\0' && curCol_ < 8U) {
        rows_[curRow_][curCol_++] = *src++;
    }
    rows_[curRow_][8] = '\0';
}

void Display::createChar(uint8_t /*slot*/, const uint8_t* /*bitmap8bytes*/) {}

void Display::resetFake() {
    std::memset(rows_, ' ', sizeof(rows_));
    rows_[0][8] = '\0';
    rows_[1][8] = '\0';
    curCol_     = 0;
    curRow_     = 0;
}
