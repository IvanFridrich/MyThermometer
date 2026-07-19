#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "quiet_hours.h"

// Quiet-hours predicate: minutes-of-day window with midnight wrap, `to`
// exclusive, `from == to` meaning disabled. Default window is 22:00-09:00.
using quiet::inQuietWindow;

namespace {
constexpr uint16_t k2200 = 22U * 60U; // 1320
constexpr uint16_t k0900 = 9U * 60U;  // 540
} // namespace

TEST_CASE("same-day window is [from, to)") {
    // 09:00-22:00 (daytime), for the daytime-quiet case.
    CHECK(inQuietWindow(12U * 60U, k0900, k2200));       // noon inside
    CHECK(inQuietWindow(k0900, k0900, k2200));           // from is inclusive
    CHECK_FALSE(inQuietWindow(k2200, k0900, k2200));     // to is exclusive
    CHECK_FALSE(inQuietWindow(8U * 60U, k0900, k2200));  // before window
    CHECK_FALSE(inQuietWindow(23U * 60U, k0900, k2200)); // after window
}

TEST_CASE("default 22:00-09:00 window wraps midnight") {
    CHECK(inQuietWindow(k2200, k2200, k0900));                 // 22:00 inclusive
    CHECK(inQuietWindow(23U * 60U, k2200, k0900));             // late evening
    CHECK(inQuietWindow(0U, k2200, k0900));                    // midnight
    CHECK(inQuietWindow(3U * 60U, k2200, k0900));              // 03:00 deep night
    CHECK(inQuietWindow(8U * 60U + 59U, k2200, k0900));        // 08:59 last quiet minute
    CHECK_FALSE(inQuietWindow(k0900, k2200, k0900));           // 09:00 to is exclusive
    CHECK_FALSE(inQuietWindow(12U * 60U, k2200, k0900));       // noon awake
    CHECK_FALSE(inQuietWindow(21U * 60U + 59U, k2200, k0900)); // 21:59 just before
}

TEST_CASE("from == to disables the window") {
    CHECK_FALSE(inQuietWindow(0U, 600U, 600U));
    CHECK_FALSE(inQuietWindow(600U, 600U, 600U));
    CHECK_FALSE(inQuietWindow(1439U, 0U, 0U));
}
