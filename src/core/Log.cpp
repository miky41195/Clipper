#include "core/Log.h"
#include <chrono>
#include <cstdio>
#include <mutex>

namespace clipper {
namespace {
std::mutex g_mu;
FILE* g_file = nullptr;
}  // namespace

void logInit(const std::filesystem::path& dir) {
  namespace fs = std::filesystem;
  std::lock_guard lk(g_mu);
  std::error_code ec;
  for (int i = 3; i >= 1; --i)
    fs::rename(dir / std::format("clipper.{}.log", i), dir / std::format("clipper.{}.log", i + 1), ec);
  fs::rename(dir / "clipper.log", dir / "clipper.1.log", ec);
  if (g_file) fclose(g_file);
  g_file = _wfopen((dir / "clipper.log").c_str(), L"wb");
}

void logLine(std::string_view level, std::string_view msg) {
  std::lock_guard lk(g_mu);
  if (!g_file) return;
  const auto t = std::chrono::current_zone()->to_local(std::chrono::system_clock::now());
  const auto line = std::format("{:%Y-%m-%d %H:%M:%S} {:5} {}\n", std::chrono::floor<std::chrono::milliseconds>(t), level, msg);
  fwrite(line.data(), 1, line.size(), g_file);
  fflush(g_file);
}

}  // namespace clipper
