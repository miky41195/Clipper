#include <windows.h>
#include <mfapi.h>
#include <shellapi.h>
#include <string_view>
#include <winrt/base.h>
#include "app/Bench.h"
#include "app/Controller.h"
#include "core/Log.h"
#include "core/Paths.h"

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  const bool bench = argc >= 3 && std::wstring_view(argv[1]) == L"--bench";

  HANDLE single = nullptr;
  if (!bench) {
    single = CreateMutexW(nullptr, TRUE, L"Local\\ClipperSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
  }
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  MFStartup(MF_VERSION);
  clipper::logInit(clipper::logDir());
  int rc = 0;
  try {
    if (bench) {
      rc = clipper::runBench(_wtoi(argv[2]));
    } else {
      clipper::Controller controller(inst);
      rc = controller.run();
    }
  } catch (const std::exception& e) {
    clipper::logError("errore fatale: {}", e.what());
    MessageBoxA(nullptr, e.what(), "Clipper", MB_ICONERROR);
    rc = 1;
  }
  MFShutdown();
  LocalFree(argv);
  if (single) CloseHandle(single);
  return rc;
}
