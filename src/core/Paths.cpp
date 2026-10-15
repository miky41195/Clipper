#include "core/Paths.h"
#include "core/Text.h"
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

namespace clipper {
namespace fs = std::filesystem;

static fs::path knownFolder(REFKNOWNFOLDERID id) {
  PWSTR p = nullptr;
  fs::path result;
  if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p))) result = p;
  CoTaskMemFree(p);
  return result;
}

static fs::path ensure(fs::path p) {
  std::error_code ec;
  fs::create_directories(p, ec);
  return p;
}

fs::path appDataDir() { return ensure(knownFolder(FOLDERID_RoamingAppData) / L"Clipper"); }
fs::path localDataDir() { return ensure(knownFolder(FOLDERID_LocalAppData) / L"Clipper"); }
fs::path defaultSettingsPath() { return appDataDir() / L"settings.json"; }
fs::path logDir() { return ensure(localDataDir() / L"logs"); }

fs::path bufferDir(const Settings& s) {
  return ensure(s.tempDir.empty() ? localDataDir() / L"buffer" : fs::path(utf8ToWide(s.tempDir)));
}

fs::path outputDir(const Settings& s) {
  return ensure(s.outputDir.empty() ? knownFolder(FOLDERID_Videos) / L"Clipper" : fs::path(utf8ToWide(s.outputDir)));
}

}  // namespace clipper
