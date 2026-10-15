#include "core/Text.h"
#include <windows.h>

namespace clipper {

std::wstring utf8ToWide(std::string_view s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
  std::wstring w(size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
  return w;
}

std::string wideToUtf8(std::wstring_view s) {
  if (s.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
  std::string u(size_t(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), u.data(), n, nullptr, nullptr);
  return u;
}

std::string toLowerAscii(std::string s) {
  for (auto& c : s)
    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
  return s;
}

}  // namespace clipper
