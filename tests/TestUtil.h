#pragma once
#include "core/Text.h"
#include <filesystem>
#include <string_view>

inline std::filesystem::path freshTempDir(std::string_view utf8Name) {
  const auto dir = std::filesystem::temp_directory_path() / "clipper_tests" / clipper::utf8ToWide(utf8Name);
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir);
  return dir;
}
