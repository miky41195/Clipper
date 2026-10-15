#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace clipper {

std::wstring utf8ToWide(std::string_view s);
std::string wideToUtf8(std::wstring_view s);
std::string toLowerAscii(std::string s);
inline std::string pathToUtf8(const std::filesystem::path& p) { return wideToUtf8(p.wstring()); }

}  // namespace clipper
