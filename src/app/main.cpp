#include <windows.h>
#include <mfapi.h>
#include <shellapi.h>
#include <exception>
#include <string_view>
#include <winrt/base.h>
#include "app/Bench.h"
#include "app/Controller.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Text.h"
#include "ui/UiPreview.h"

static void onTerminate() {
  std::string what = "terminate senza eccezione";
  if (const auto e = std::current_exception()) {
    try {
      std::rethrow_exception(e);
    } catch (const std::exception& ex) {
      what = ex.what();
    } catch (const winrt::hresult_error& ex) {
      what = winrt::to_string(ex.message());
    } catch (...) {
      what = "eccezione sconosciuta";
    }
  }
  clipper::logError("terminazione inattesa: {}", what);
  std::abort();
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
  std::set_terminate(onTerminate);
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  const bool bench = argc >= 3 && std::wstring_view(argv[1]) == L"--bench";
  const bool renderUi = argc >= 4 && std::wstring_view(argv[1]) == L"--render-ui";
  bool showUi = false;
  for (int i = 1; i < argc; ++i) showUi |= std::wstring_view(argv[i]) == L"--show";

  HANDLE single = nullptr;
  if (!bench && !renderUi) {
    single = CreateMutexW(nullptr, TRUE, L"Local\\ClipperSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
      // Already running: ask that instance to open its window.
      if (HWND other = FindWindowW(L"ClipperController", nullptr)) PostMessageW(other, WM_APP + 3, 0, 0);
      return 0;
    }
  }
  if (renderUi) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    const float scale = argc >= 5 ? float(_wtof(argv[4])) : 1.f;
    return clipper::renderPreview(clipper::wideToUtf8(argv[2]), argv[3], scale) ? 0 : 1;
  }
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  MFStartup(MF_VERSION);
  clipper::logInit(clipper::logDir());
  int rc = 0;
  try {
    if (bench) {
      rc = clipper::runBench(_wtoi(argv[2]));
    } else {
      clipper::Controller controller(inst, showUi);
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
