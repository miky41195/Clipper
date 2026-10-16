#include "platform/Autostart.h"
#include <windows.h>

namespace clipper {
namespace {
const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
}

bool autostartEnabled(const wchar_t* valueName) {
  return RegGetValueW(HKEY_CURRENT_USER, kRunKey, valueName, RRF_RT_REG_SZ, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}

void setAutostart(bool enabled, const std::wstring& exePath, const wchar_t* valueName) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
  if (enabled) {
    const std::wstring cmd = L"\"" + exePath + L"\"";
    RegSetValueExW(key, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                   DWORD((cmd.size() + 1) * sizeof(wchar_t)));
  } else {
    RegDeleteValueW(key, valueName);
  }
  RegCloseKey(key);
}

}  // namespace clipper
