#pragma once
#include "core/Settings.h"
#include "platform/Hotkeys.h"
#include "platform/Toast.h"
#include "platform/Tray.h"
#include "session/CaptureSession.h"
#include "session/Recorder.h"
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <windows.h>

namespace clipper {

// Owns the app: hidden message window, tray, hotkeys, toast, capture session, recorder and a
// sequential save worker. All UI state is touched only on the thread that calls run().
class Controller {
public:
  explicit Controller(HINSTANCE inst);
  ~Controller();
  Controller(const Controller&) = delete;
  Controller& operator=(const Controller&) = delete;

  int run();

private:
  static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);

  void startSession();
  void saveClip();
  void toggleRecord();
  void togglePause();
  void reloadSettings();
  void openSettings();
  void openFolder();
  void applyHotkeys();
  void runMenu();
  void notify(const std::wstring& title, const std::wstring& body, ToastKind kind = ToastKind::Info);
  void warnAsync(const std::string& msg);
  void updateTray();
  void post(std::function<void()> fn);     // run on the UI thread
  void enqueue(std::function<void()> job);  // run on the save worker
  void workerLoop();

  HINSTANCE inst_;
  HWND hwnd_ = nullptr;
  UINT taskbarCreatedMsg_ = 0;
  std::filesystem::path settingsPath_;
  Settings settings_;
  bool paused_ = false;
  std::unique_ptr<TrayIcon> tray_;
  std::unique_ptr<HotkeyManager> hotkeys_;
  std::unique_ptr<Toast> toast_;
  std::unique_ptr<CaptureSession> session_;
  std::unique_ptr<Recorder> recorder_;

  std::mutex jobMu_;
  std::condition_variable jobCv_;
  std::deque<std::function<void()>> jobs_;
  bool stopWorker_ = false;
  std::thread worker_;
};

}  // namespace clipper
