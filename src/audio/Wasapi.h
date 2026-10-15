#pragma once
#include "audio/PcmSource.h"
#include "audio/ProcessTree.h"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace clipper {

// Shared capture thread for WASAPI clients: 48 kHz stereo s16, event driven, QPC timestamps.
class WasapiCapture {
public:
  ~WasapiCapture();
  bool failed() const { return failed_; }

protected:
  WasapiCapture() = default;
  void start(std::function<void*()> activate, bool loopback, std::shared_ptr<PcmSource> sink);  // throws on init error

private:
  std::shared_ptr<PcmSource> sink_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> failed_{false};
  std::thread thread_;
};

// Captures the audio of one process tree (Windows 10 2004+ process loopback).
class LoopbackCapture : public WasapiCapture {
public:
  LoopbackCapture(uint32_t pid, std::shared_ptr<PcmSource> sink);
};

// Captures a microphone; empty id = default communications/console capture device.
class MicCapture : public WasapiCapture {
public:
  MicCapture(const std::string& deviceId, std::shared_ptr<PcmSource> sink);
};

std::vector<std::pair<std::string, std::string>> listMicrophones();  // {id, friendly name}
// PIDs owning audio sessions on any active render endpoint; activeOnly = currently playing.
std::vector<uint32_t> audioSessionPids(bool activeOnly = true);
std::vector<ProcEntry> snapshotProcesses();
std::string appDisplayName(uint32_t pid, const std::string& exe);

}  // namespace clipper
