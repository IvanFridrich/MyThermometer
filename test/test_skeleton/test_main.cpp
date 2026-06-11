// test/native/test_main.cpp
//
// Phase-0 skeleton test — verifies that the native build environment compiles
// and that doctest runs under ASan/UBSan.  No Arduino or ESP-IDF headers are
// included here; this file must build with plain Clang (-fno-exceptions,
// -fno-rtti, -fsanitize=address,undefined).
//
// Real domain tests will be added alongside each src/core/ module.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

TEST_CASE("skeleton compiles and runs") {
    CHECK(1 + 1 == 2);
}
