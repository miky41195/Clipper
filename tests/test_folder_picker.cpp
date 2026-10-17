#include <doctest/doctest.h>
#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include "TestUtil.h"
#include "platform/FolderPicker.h"

using namespace clipper;

namespace {
// Closes the first dialog (#32770) of this process that becomes visible.
void closeDialogWhenShown(std::atomic<bool>& closed) {
  for (int i = 0; i < 200 && !closed; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EnumWindows(
        [](HWND h, LPARAM p) -> BOOL {
          DWORD pid = 0;
          GetWindowThreadProcessId(h, &pid);
          wchar_t cls[16]{};
          GetClassNameW(h, cls, 16);
          if (pid == GetCurrentProcessId() && IsWindowVisible(h) && std::wstring_view(cls) == L"#32770") {
            PostMessageW(h, WM_COMMAND, IDCANCEL, 0);
            *reinterpret_cast<std::atomic<bool>*>(p) = true;
            return FALSE;
          }
          return TRUE;
        },
        reinterpret_cast<LPARAM>(&closed));
  }
}
}  // namespace

TEST_SUITE("desktop") {
  TEST_CASE("folder picker opens from the (MTA) UI thread and can be cancelled") {
    HWND owner = CreateWindowExW(0, L"STATIC", L"owner", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    std::atomic<bool> done{false}, closed{false};
    std::thread watchdog([&] {
      for (int i = 0; i < 150 && !done; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
      if (!done) {
        std::fputs("folder picker HUNG\n", stderr);
        std::fflush(stderr);
        TerminateProcess(GetCurrentProcess(), 3);
      }
    });
    std::thread closer([&] { closeDialogWhenShown(closed); });
    const auto result = pickFolder(owner, freshTempDir("picker"));
    done = true;
    closer.join();
    watchdog.join();
    DestroyWindow(owner);
    CHECK(closed);
    CHECK_FALSE(result.has_value());
  }
}
