#pragma once
#include "core/Settings.h"
#include <string>

namespace clipper {

// Draft being edited in the Settings page vs. what is applied.
struct SettingsForm {
  explicit SettingsForm(const Settings& s) : draft(s), saved(s) {}
  bool dirty() const;
  void revert() { draft = saved; }
  void markSaved() { saved = draft; }
  Settings draft;
  Settings saved;
};

std::string durationLabel(int seconds);
// captureW/H = monitor size; audioTracks = mix + mic + apps currently playing.
std::string estimateLine(const Settings& s, int captureW, int captureH, int audioTracks);
std::string recordingLine(const Settings& s, int captureW, int captureH, int audioTracks);
std::string bufferLine(const Settings& s, int captureW, int captureH, int audioTracks);
int videoKbpsFor(const Settings& s, int captureW, int captureH);

}  // namespace clipper
