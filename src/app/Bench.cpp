#include "app/Bench.h"
#include "core/Paths.h"
#include "core/SizeEstimate.h"
#include "core/Text.h"
#include "output/ClipWriter.h"
#include "session/CaptureSession.h"
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <cstdio>
#include <format>
#include <fstream>

namespace clipper {
namespace {

uint64_t fileTimeTicks(const FILETIME& f) { return (uint64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime; }

uint64_t processCpuTicks() {
  FILETIME c, e, k, u;
  GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
  return fileTimeTicks(k) + fileTimeTicks(u);
}

PROCESS_MEMORY_COUNTERS_EX memory() {
  PROCESS_MEMORY_COUNTERS_EX pmc{};
  pmc.cb = sizeof(pmc);
  GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc));
  return pmc;
}

}  // namespace

int runBench(int secs) {
  if (AttachConsole(ATTACH_PARENT_PROCESS)) {
    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
  }
  Settings s = loadSettings(defaultSettingsPath());
  std::string warnings;
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  const double cores = si.dwNumberOfProcessors;

  double cpuSum = 0, cpuMax = 0;
  size_t ramMax = 0, ramNoBufferMax = 0, wsNoBufferMax = 0;
  int samples = 0;
  std::string clipInfo;
  {
    CaptureSession session(s, [&](const std::string& w) { warnings += w + "; "; });
    Sleep(2000);  // warm-up excluded from stats
    uint64_t lastCpu = processCpuTicks();
    Ticks lastWall = now();
    for (int i = 0; i < secs; ++i) {
      Sleep(1000);
      const uint64_t cpu = processCpuTicks();
      const Ticks wall = now();
      const double pct = 100.0 * double(cpu - lastCpu) / double(wall - lastWall) / cores;
      lastCpu = cpu;
      lastWall = wall;
      cpuSum += pct;
      cpuMax = std::max(cpuMax, pct);
      const auto m = memory();
      const size_t ram = m.PrivateUsage;
      const size_t buffer = session.bufferRamBytes();
      ramMax = std::max(ramMax, ram);
      ramNoBufferMax = std::max(ramNoBufferMax, ram - std::min(ram, buffer));
      wsNoBufferMax = std::max(wsNoBufferMax, m.WorkingSetSize - std::min<size_t>(m.WorkingSetSize, buffer));
      ++samples;
    }
    const auto snap = session.snapshot(seconds(s.replaySeconds));
    const auto path = outputDir(s) / L"bench_clip.mp4";
    const Ticks t0 = now();
    try {
      writeSnapshot(snap, path, "mp4");
      const double ms = double(now() - t0) / 10'000.0;
      clipInfo = std::format("clip {:.1f} s, {} , salvata in {:.0f} ms, {} tracce -> {}", double(snap.end - snap.start) / kTicksPerSecond,
                             formatBytes(std::filesystem::file_size(path)), ms, snap.tracks.size(), pathToUtf8(path));
    } catch (const std::exception& e) {
      clipInfo = std::string("salvataggio fallito: ") + e.what();
    }
  }
  const std::string report = std::format(
      "Clipper bench ({} s, {} fps, codec {}, buffer {} s)\n"
      "CPU media {:.2f}%  max {:.2f}%  (obiettivo <= 2%)\n"
      "RAM privata max {}  senza buffer {}  working set senza buffer {}\n"
      "{}\n"
      "avvisi: {}\n",
      samples, s.fps, int(s.codec), s.replaySeconds, samples ? cpuSum / samples : 0.0, cpuMax, formatBytes(ramMax),
      formatBytes(ramNoBufferMax), formatBytes(wsNoBufferMax), clipInfo, warnings.empty() ? "nessuno" : warnings);
  fputs(report.c_str(), stdout);
  fflush(stdout);
  std::ofstream(localDataDir() / L"bench.txt", std::ios::binary) << report;
  return 0;
}

}  // namespace clipper
