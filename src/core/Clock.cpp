#include "core/Clock.h"
#include <windows.h>

namespace clipper {

Ticks qpcToTicks(int64_t qpc, int64_t freq) {
  return (qpc / freq) * kTicksPerSecond + (qpc % freq) * kTicksPerSecond / freq;
}

Ticks now() {
  static const int64_t freq = [] {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return f.QuadPart;
  }();
  LARGE_INTEGER c;
  QueryPerformanceCounter(&c);
  return qpcToTicks(c.QuadPart, freq);
}

}  // namespace clipper
