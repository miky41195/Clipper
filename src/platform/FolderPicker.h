#pragma once
#include <windows.h>
#include <filesystem>
#include <optional>

namespace clipper {

std::optional<std::filesystem::path> pickFolder(HWND owner, const std::filesystem::path& initial);

}  // namespace clipper
