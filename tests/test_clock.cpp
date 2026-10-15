#include <doctest/doctest.h>
#include <windows.h>
#include "core/Clock.h"

using namespace clipper;

TEST_CASE("qpcToTicks converts exactly and without overflow") {
  CHECK(qpcToTicks(10'000'000, 10'000'000) == 10'000'000);
  CHECK(qpcToTicks(3, 3) == kTicksPerSecond);
  CHECK(qpcToTicks(1, 3) == 3'333'333);
  const int64_t freq = 24'000'000;
  const int64_t hundredDays = freq * 86'400 * 100;
  CHECK(qpcToTicks(hundredDays, freq) == int64_t{86'400} * 100 * kTicksPerSecond);
}

TEST_CASE("seconds helper") {
  CHECK(seconds(1.5) == 15'000'000);
}

TEST_CASE("now is monotonic") {
  const Ticks a = now();
  Sleep(2);
  CHECK(now() > a);
}
