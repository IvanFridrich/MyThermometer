#pragma once
#include <cstdint>

#include "result.h"

// HAL: HD44780 2×8 LCD in 4-bit mode.
// Target: LiquidCrystal Arduino library (Phase 3).  Fake: in-memory row buffers.
class Display {
  public:
    Display(uint8_t rs, uint8_t en, uint8_t d4, uint8_t d5, uint8_t d6, uint8_t d7);

    Result<void> init();
    void         clear();
    void         setCursor(uint8_t col, uint8_t row);
    void         print(const char* text);

    // Load a custom character (e.g. degree symbol) into CGRAM slot.
    void createChar(uint8_t slot, const uint8_t* bitmap8bytes);

#ifdef NATIVE_BUILD
  public:
    // Inspection API — row content after print() calls, null-terminated.
    const char* row(uint8_t r) const { return (r < 2U) ? rows_[r] : ""; }
    void        resetFake();

  private:
    char    rows_[2][9]{}; // 8 chars + NUL per row
    uint8_t curCol_{0};
    uint8_t curRow_{0};
#endif
};
