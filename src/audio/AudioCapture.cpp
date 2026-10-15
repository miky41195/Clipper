#include "audio/AudioCapture.h"
#include <windows.h>
#include <combaseapi.h>
#include <algorithm>

namespace clipper {
namespace {
constexpr Ticks kGoneAfter = 5 * kTicksPerSecond;
}

AudioCapture::AudioCapture(AudioHub& hub, AudioCaptureConfig cfg, WarnSink onWarning)
    : hub_(hub), cfg_(std::move(cfg)), onWarning_(std::move(onWarning)) {
  if (cfg_.mic) {
    micTrack_ = hub_.addTrack("Microfono");
    micSource_ = std::make_shared<PcmSource>();
    try {
      mic_ = std::make_unique<MicCapture>(cfg_.micDeviceId, micSource_);
      hub_.attach(micTrack_, micSource_);
    } catch (...) {
      hub_.removeTrack(micTrack_);
      micTrack_ = -1;
      if (onWarning_) onWarning_("Microfono non disponibile: registro senza voce");
    }
  }
  thread_ = std::thread([this] {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::unique_lock lk(mu_);
    while (!stop_) {
      lk.unlock();
      try {
        refresh();
      } catch (...) {
      }
      lk.lock();
      cv_.wait_for(lk, std::chrono::seconds(1), [&] { return stop_; });
    }
    lk.unlock();
    for (auto& [exe, app] : apps_) dropApp(app);
    apps_.clear();
    CoUninitialize();
  });
}

AudioCapture::~AudioCapture() {
  {
    std::lock_guard lk(mu_);
    stop_ = true;
  }
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
  mic_.reset();
}

void AudioCapture::dropApp(App& app) {
  for (auto& [pid, cap] : app.captures) hub_.detach(app.track, cap.second);
  app.captures.clear();
  if (app.track != hub_.othersTrackId()) hub_.removeTrack(app.track);
}

void AudioCapture::refresh() {
  const auto groups = groupAudioApps(audioSessionPids(), snapshotProcesses(), cfg_.blacklist, GetCurrentProcessId());
  const Ticks t = now();
  for (const auto& g : groups) {
    auto it = apps_.find(g.exe);
    if (it == apps_.end()) {
      App app;
      app.track = hub_.trackForNewApp(appDisplayName(g.roots.front(), g.exe));
      it = apps_.emplace(g.exe, std::move(app)).first;
    }
    App& app = it->second;
    app.lastSeen = t;
    for (uint32_t root : g.roots) {
      if (app.captures.contains(root) || failedPids_.contains(root)) continue;
      auto src = std::make_shared<PcmSource>();
      try {
        auto cap = std::make_unique<LoopbackCapture>(root, src);
        hub_.attach(app.track, src);
        app.captures.emplace(root, std::make_pair(std::move(cap), src));
      } catch (...) {
        failedPids_.insert(root);
      }
    }
    // Roots that are no longer part of the group (process exited) or whose capture died.
    for (auto c = app.captures.begin(); c != app.captures.end();) {
      const bool present = std::find(g.roots.begin(), g.roots.end(), c->first) != g.roots.end();
      if (!present || c->second.first->failed()) {
        hub_.detach(app.track, c->second.second);
        c = app.captures.erase(c);
      } else {
        ++c;
      }
    }
  }
  for (auto it = apps_.begin(); it != apps_.end();) {
    if (t - it->second.lastSeen > kGoneAfter) {
      dropApp(it->second);
      it = apps_.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace clipper
