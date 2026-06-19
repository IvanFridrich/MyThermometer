#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "moving_average.h"
#include "types.h"

// Phase 2 domain test: MovingAverage<N> — fixed-window integer average that
// excludes kTempInvalid samples.  Covers warm-up, wrap-around eviction,
// all-invalid windows, negative temperatures and integer-division behaviour.

TEST_CASE("empty average is invalid") {
    MovingAverage<10> avg;
    CHECK(avg.average() == kTempInvalid);
    CHECK(avg.validCount() == 0);
    CHECK_FALSE(avg.isFull());
    CHECK(avg.capacity() == 10);
}

TEST_CASE("single sample averages to itself") {
    MovingAverage<10> avg;
    avg.push(2350);
    CHECK(avg.average() == 2350);
    CHECK(avg.validCount() == 1);
}

TEST_CASE("warm-up averages only the samples seen so far") {
    MovingAverage<10> avg;
    avg.push(1000);
    avg.push(2000);
    avg.push(3000);
    CHECK(avg.average() == 2000); // (1000+2000+3000)/3
    CHECK(avg.validCount() == 3);
    CHECK_FALSE(avg.isFull());
}

TEST_CASE("becomes full at exactly N samples") {
    MovingAverage<3> avg;
    avg.push(100);
    avg.push(200);
    CHECK_FALSE(avg.isFull());
    avg.push(300);
    CHECK(avg.isFull());
    CHECK(avg.average() == 200);
}

TEST_CASE("wrap-around evicts the oldest sample") {
    MovingAverage<3> avg;
    avg.push(100);
    avg.push(200);
    avg.push(300); // window: [100,200,300] -> 200
    CHECK(avg.average() == 200);
    avg.push(600); // evicts 100; window: [200,300,600] -> 366 (1100/3 trunc)
    CHECK(avg.isFull());
    CHECK(avg.validCount() == 3);
    CHECK(avg.average() == 366);
}

TEST_CASE("many wraps keep only the last N samples") {
    MovingAverage<3> avg;
    for (Temperature t = 0; t <= 1000; t = static_cast<Temperature>(t + 100)) {
        avg.push(t);
    }
    // Last three pushed: 800, 900, 1000 -> 900
    CHECK(avg.average() == 900);
    CHECK(avg.validCount() == 3);
}

TEST_CASE("invalid samples are excluded from the average") {
    MovingAverage<5> avg;
    avg.push(1000);
    avg.push(kTempInvalid);
    avg.push(2000);
    avg.push(kTempInvalid);
    avg.push(3000);
    CHECK(avg.average() == 2000); // (1000+2000+3000)/3
    CHECK(avg.validCount() == 3);
    CHECK(avg.isFull()); // count tracks pushes, including invalid ones
}

TEST_CASE("a window of only invalid samples is invalid") {
    MovingAverage<3> avg;
    avg.push(kTempInvalid);
    avg.push(kTempInvalid);
    CHECK(avg.average() == kTempInvalid);
    CHECK(avg.validCount() == 0);
}

TEST_CASE("a valid sample evicted by invalids yields invalid average") {
    MovingAverage<2> avg;
    avg.push(2500);
    CHECK(avg.average() == 2500);
    avg.push(kTempInvalid);
    avg.push(kTempInvalid); // evicts the 2500
    CHECK(avg.average() == kTempInvalid);
    CHECK(avg.validCount() == 0);
}

TEST_CASE("negative temperatures average correctly") {
    MovingAverage<4> avg;
    avg.push(-1000); // -10.00 C
    avg.push(-2000); // -20.00 C
    CHECK(avg.average() == -1500);
    CHECK(avg.validCount() == 2);
}

TEST_CASE("mixed sign averages around zero") {
    MovingAverage<4> avg;
    avg.push(-1000);
    avg.push(1000);
    CHECK(avg.average() == 0);
}

TEST_CASE("integer division truncates toward zero") {
    MovingAverage<2> avg;
    avg.push(0);
    avg.push(1); // 1/2 -> 0
    CHECK(avg.average() == 0);

    MovingAverage<2> neg;
    neg.push(0);
    neg.push(-1); // -1/2 -> 0 (truncation toward zero)
    CHECK(neg.average() == 0);
}

TEST_CASE("reset clears the window") {
    MovingAverage<3> avg;
    avg.push(1000);
    avg.push(2000);
    avg.reset();
    CHECK(avg.average() == kTempInvalid);
    CHECK(avg.validCount() == 0);
    CHECK_FALSE(avg.isFull());
    avg.push(500);
    CHECK(avg.average() == 500);
}

TEST_CASE("10-sample window matches the production averaging window") {
    // FR-02: 10-minute floating average = 10 one-per-minute samples.
    MovingAverage<10> avg;
    for (int i = 1; i <= 10; ++i) {
        avg.push(static_cast<Temperature>(i * 100)); // 100..1000
    }
    CHECK(avg.isFull());
    CHECK(avg.average() == 550); // (100+...+1000)/10 = 5500/10
}
