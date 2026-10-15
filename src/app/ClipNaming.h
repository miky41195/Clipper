#pragma once
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

namespace clipper {

// {app}, {date} (YYYY-MM-DD) and {time} (HH-MM-SS) in local time.
std::string expandNameTemplate(std::string_view tpl, std::string_view app, std::chrono::system_clock::time_point t);
std::string sanitizeFileName(std::string_view s);
// dir/stem+ext, or dir/"stem (N)"+ext for the first N >= 2 that does not exist. stem/ext are UTF-8.
std::filesystem::path uniquePath(const std::filesystem::path& dir, const std::string& stem, const std::string& ext);

}  // namespace clipper
