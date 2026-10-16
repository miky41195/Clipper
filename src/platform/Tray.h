#pragma once
#include "core/Settings.h"
#include <windows.h>
#include <string>

namespace clipper {

enum class TrayState { Buffering, Recording, Paused, Error };

enum MenuCmd : UINT {
  CmdSave = 100,
  CmdRecord,
  CmdPause,
  CmdGallery,
  CmdSettings,
  CmdOpenFolder,
  CmdQuit,
};

class TrayIcon {
public:
  static constexpr UINT kCallbackMsg = WM_APP + 1;

  explicit TrayIcon(HWND hwnd);
  ~TrayIcon();
  TrayIcon(const TrayIcon&) = delete;
  TrayIcon& operator=(const TrayIcon&) = delete;

  void setState(TrayState s, const std::wstring& tooltip);
  UINT showMenu(bool recording, bool paused, const HotkeySettings& hk);  // returns MenuCmd or 0
  void recreate();  // after Explorer restarts (TaskbarCreated)

private:
  void add();
  HWND hwnd_;
  HICON icons_[4]{};
  TrayState state_ = TrayState::Buffering;
  std::wstring tip_ = L"Clipper";
};

}  // namespace clipper
