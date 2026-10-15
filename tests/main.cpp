#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include <windows.h>
#include <mfapi.h>
#include <winrt/base.h>

int main(int argc, char** argv) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  MFStartup(MF_VERSION);
  doctest::Context ctx(argc, argv);
  const int rc = ctx.run();
  MFShutdown();
  return rc;
}
