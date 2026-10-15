#pragma once
#include <filesystem>
#include <format>
#include <string_view>

namespace clipper {

void logInit(const std::filesystem::path& dir);  // clipper.log, keeps 4 previous runs
void logLine(std::string_view level, std::string_view msg);

template <class... A>
void logInfo(std::format_string<A...> f, A&&... a) {
  logLine("INFO", std::format(f, std::forward<A>(a)...));
}
template <class... A>
void logWarn(std::format_string<A...> f, A&&... a) {
  logLine("WARN", std::format(f, std::forward<A>(a)...));
}
template <class... A>
void logError(std::format_string<A...> f, A&&... a) {
  logLine("ERROR", std::format(f, std::forward<A>(a)...));
}

}  // namespace clipper
