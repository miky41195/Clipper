#pragma once
#include <cstdint>

namespace clipper {

using Ticks = int64_t;  // 100 ns units, same unit as WinRT TimeSpan and MF sample times
constexpr Ticks kTicksPerSecond = 10'000'000;

constexpr Ticks seconds(double s) { return static_cast<Ticks>(s * kTicksPerSecond); }

Ticks qpcToTicks(int64_t qpc, int64_t freq);
Ticks now();

}  // namespace clipper
