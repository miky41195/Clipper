#include "app/Startup.h"
#include "audio/Wasapi.h"
#include "core/Text.h"
#include <windows.h>
#include <vector>

namespace clipper {
namespace fs = std::filesystem;

void cleanupOnStartup(const fs::path& bufferDir, const fs::path& outputDir) {
  std::error_code ec;
  std::vector<fs::path> files;
  for (const auto& e : fs::directory_iterator(bufferDir, ec))
    if (e.is_regular_file()) files.push_back(e.path());
  for (const auto& f : files) fs::remove(f, ec);

  files.clear();
  for (const auto& e : fs::directory_iterator(outputDir, ec))
    if (e.is_regular_file()) files.push_back(e.path());
  const std::wstring recSuffix = L".rec.mkv.tmp";
  for (const auto& f : files) {
    const std::wstring name = f.filename().wstring();
    if (name.ends_with(recSuffix)) {
      fs::rename(f, outputDir / (name.substr(0, name.size() - recSuffix.size()) + L"_recuperata.mkv"), ec);
    } else if (name.ends_with(L".tmp")) {
      fs::remove(f, ec);
    }
  }
}

std::string foregroundAppName() {
  HWND w = GetForegroundWindow();
  if (!w) return "Desktop";
  wchar_t cls[64]{};
  GetClassNameW(w, cls, 64);
  const std::wstring_view c(cls);
  if (c == L"Progman" || c == L"WorkerW" || c == L"Shell_TrayWnd") return "Desktop";
  DWORD pid = 0;
  GetWindowThreadProcessId(w, &pid);
  std::string exe;
  if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
    wchar_t path[MAX_PATH * 2];
    DWORD len = DWORD(std::size(path));
    if (QueryFullProcessImageNameW(h, 0, path, &len)) exe = toLowerAscii(pathToUtf8(fs::path(path).filename()));
    CloseHandle(h);
  }
  if (exe.empty()) return "Desktop";
  return appDisplayName(pid, exe);
}

}  // namespace clipper
