#pragma once
#include <cstdint>

// Relative-time abstraction.  Target implementation wraps Arduino millis();
// fake implementation uses a manually advanceable counter for deterministic tests.
//
// NATIVE_BUILD exception (see docs/adr-phase1.md ADR-09): Clock carries its fake
// injection API here because the target has no data members — a separate fake header
// would be an unnecessary layer.  All Clock instances share ONE counter in native
// builds (global in clock_fake.cpp).  Call reset() at the top of every TEST_CASE
// that uses Clock to prevent inter-test interference.
class Clock {
  public:
    uint32_t millis();

#ifdef NATIVE_BUILD
    // Test-only controls (implemented in clock_fake.cpp).
    void advanceMs(uint32_t delta);
    void reset();
#endif
};
