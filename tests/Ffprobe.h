#pragma once
#include <cstdio>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

// Runs ffprobe and returns its JSON (streams + format).
inline nlohmann::json ffprobe(const std::filesystem::path& file) {
  const std::wstring cmd = L"\"\"" + std::filesystem::path(CLIPPER_FFPROBE).wstring() +
                           L"\" -v error -show_streams -show_format -of json \"" + file.wstring() + L"\"\"";
  FILE* p = _wpopen(cmd.c_str(), L"rb");
  if (!p) return {};
  std::string out;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
  _pclose(p);
  return nlohmann::json::parse(out, nullptr, false);
}

inline double probeDuration(const nlohmann::json& j) { return std::stod(j["format"]["duration"].get<std::string>()); }

inline std::string streamTag(const nlohmann::json& s, const char* key) {
  if (!s.contains("tags")) return {};
  for (auto& [k, v] : s["tags"].items())
    if (_stricmp(k.c_str(), key) == 0) return v.get<std::string>();
  return {};
}
