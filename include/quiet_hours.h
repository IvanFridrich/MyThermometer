#pragma once
#include <cstdint>

// Quiet-hours predicate — pure domain, host-testable. Window-advice melodies
// are suppressed while the local time-of-day falls inside [from, to). All
// values are minutes-of-day (0..1439).
namespace quiet {

// True when nowMin lies in the quiet window.
//   from < to : same-day window,  [from, to)
//   from > to : wraps midnight,    now >= from || now < to  (e.g. 22:00-09:00)
//   from == to: disabled (never quiet)
// `to` is exclusive so a 1-minute-resolution window has no off-by-one overlap.
// Single-return form so it stays constexpr under the C++11 esp32 build.
[[nodiscard]] constexpr bool inQuietWindow(uint16_t nowMin, uint16_t fromMin, uint16_t toMin) {
    return (fromMin == toMin)  ? false                                  // disabled
           : (fromMin < toMin) ? (nowMin >= fromMin && nowMin < toMin)  // same day
                               : (nowMin >= fromMin || nowMin < toMin); // wraps midnight
}

} // namespace quiet
