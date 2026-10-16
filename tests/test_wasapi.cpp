#include <doctest/doctest.h>
#include <windows.h>
#include <algorithm>
#include <cstdlib>
#include <string_view>
#include <vector>
#include "audio/Wasapi.h"

using namespace clipper;

TEST_SUITE("audio-device") {
  TEST_CASE("process loopback captures a playing app") {
    // Plays a sound out loud: only when explicitly requested.
    char flag[8]{};
    size_t len = 0;
    if (getenv_s(&len, flag, "CLIPPER_AUDIO_TESTS") != 0 || std::string_view(flag) != "1") {
      MESSAGE("skipped: set CLIPPER_AUDIO_TESTS=1 to run (plays audio)");
      return;
    }
    wchar_t cmd[] =
        L"powershell -NoProfile -Command \"$p=New-Object Media.SoundPlayer 'C:\\Windows\\Media\\Alarm01.wav'; "
        L"$p.PlaySync(); $p.PlaySync()\"";
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    REQUIRE(CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi));
    Sleep(1500);
    const auto pids = audioSessionPids();
    CHECK(std::find(pids.begin(), pids.end(), pi.dwProcessId) != pids.end());
    auto src = std::make_shared<PcmSource>();
    {
      LoopbackCapture cap(pi.dwProcessId, src);
      Sleep(1500);
      CHECK_FALSE(cap.failed());
    }
    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    const size_t frames = src->bufferedFrames();
    MESSAGE("buffered frames: " << frames);
    REQUIRE(frames > 0);
    std::vector<int16_t> buf(frames * 2);
    src->pullAt(buf.data(), frames, src->headTimestamp());
    int peak = 0;
    for (auto v : buf) peak = std::max(peak, std::abs(int(v)));
    MESSAGE("peak: " << peak);
    CHECK(peak > 500);
  }

  TEST_CASE("appDisplayName falls back to the exe stem") {
    CHECK(appDisplayName(0, "mygame.exe") == "Mygame");
  }

  TEST_CASE("snapshotProcesses contains this process") {
    const auto procs = snapshotProcesses();
    const auto self = GetCurrentProcessId();
    CHECK(std::any_of(procs.begin(), procs.end(), [&](const ProcEntry& p) { return p.pid == self; }));
  }
}
