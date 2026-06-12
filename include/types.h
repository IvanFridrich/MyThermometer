#pragma once
#include <climits>
#include <cstdint>

// Temperature stored as centi-degrees Celsius (int16_t).
// Range: -3000..8000 covers -30..80 °C.  INT16_MIN = invalid/unavailable.
using Temperature                  = int16_t;
constexpr Temperature kTempInvalid = static_cast<Temperature>(INT16_MIN);

// Bitfield of event flags (cfg::flag::k* constants).
using EventFlags = uint16_t;
