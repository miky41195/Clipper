#include "app/ClipNaming.h"
#include "core/Text.h"
#include <ctime>
#include <format>

namespace clipper {
namespace fs = std::filesystem;

static void replaceAll(std::string& s, std::string_view from, std::string_view to) {
  for (size_t pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + to.size())) s.replace(pos, from.size(), to);
}

std::string expandNameTemplate(std::string_view tpl, std::string_view app, std::chrono::system_clock::time_point t) {
  const std::time_t tt = std::chrono::system_clock::to_time_t(t);
  std::tm lt{};
  localtime_s(&lt, &tt);
  char date[16], time[16];
  std::strftime(date, sizeof date, "%Y-%m-%d", &lt);
  std::strftime(time, sizeof time, "%H-%M-%S", &lt);
  std::string out(tpl);
  replaceAll(out, "{app}", app);
  replaceAll(out, "{date}", date);
  replaceAll(out, "{time}", time);
  return out;
}

std::string sanitizeFileName(std::string_view s) {
  std::string out;
  for (const char c : s) {
    const auto u = static_cast<unsigned char>(c);
    const bool bad = u < 32 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' ||
                     c == '?' || c == '*';
    out += bad ? '_' : c;
  }
  while (!out.empty() && out.front() == ' ') out.erase(out.begin());
  while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
  return out.empty() ? "clip" : out;
}

fs::path uniquePath(const fs::path& dir, const std::string& stem, const std::string& ext) {
  fs::path p = dir / utf8ToWide(stem + ext);
  for (int n = 2; fs::exists(p); ++n) p = dir / utf8ToWide(std::format("{} ({}){}", stem, n, ext));
  return p;
}

}  // namespace clipper
