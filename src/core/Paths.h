#pragma once
#include "core/Settings.h"
#include <filesystem>

namespace clipper {

std::filesystem::path appDataDir();    // %APPDATA%\Clipper
std::filesystem::path localDataDir();  // %LOCALAPPDATA%\Clipper
std::filesystem::path defaultSettingsPath();
std::filesystem::path bufferDir(const Settings& s);
std::filesystem::path outputDir(const Settings& s);
std::filesystem::path logDir();

}  // namespace clipper
