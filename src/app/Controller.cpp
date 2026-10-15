#include "app/Controller.h"
#include "app/ClipNaming.h"
#include "app/Startup.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/SizeEstimate.h"
#include "core/Text.h"
#include "output/ClipWriter.h"
#include <shellapi.h>
#include <mmsystem.h>
#include <format>

namespace clipper {
namespace fs = std::filesystem;
namespace {
constexpr UINT kRunMsg = WM_APP + 2;
const wchar_t* kWindowClass = L"ClipperController";

std::wstring w(const std::string& s) { return utf8ToWide(s); }

void playSaveSound() { PlaySoundW(L"SystemNotification", nullptr, SND_ALIAS | SND_ASYNC | SND_NODEFAULT); }

std::string durationText(Ticks t) {
  const auto secs = int((t + kTicksPerSecond / 2) / kTicksPerSecond);
  return secs >= 60 ? std::format("{}:{:02} min", secs / 60, secs % 60) : std::format("{} s", secs);
}
}  // namespace

Controller::Controller(HINSTANCE inst) : inst_(inst) {
  settingsPath_ = defaultSettingsPath();
  settings_ = loadSettings(settingsPath_);
  if (!fs::exists(settingsPath_)) saveSettings(settings_, settingsPath_);

  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = wndProc;
  wc.hInstance = inst_;
  wc.lpszClassName = kWindowClass;
  RegisterClassExW(&wc);
  hwnd_ = CreateWindowExW(0, kWindowClass, L"Clipper", 0, 0, 0, 0, 0, nullptr, nullptr, inst_, this);
  taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");

  worker_ = std::thread([this] { workerLoop(); });
  toast_ = std::make_unique<Toast>();
  tray_ = std::make_unique<TrayIcon>(hwnd_);
  hotkeys_ = std::make_unique<HotkeyManager>(hwnd_);
  applyHotkeys();
  // Only at process start: later it could delete a clip the save worker is still writing.
  cleanupOnStartup(bufferDir(settings_), outputDir(settings_));
  startSession();
  if (session_)
    notify(L"Clipper attivo",
           w(std::format("Ultimi {} pronti · {} per salvare", durationText(seconds(settings_.replaySeconds)),
                         settings_.hotkeys.saveClip)));
}

Controller::~Controller() {
  recorder_.reset();  // finalizes an ongoing recording
  session_.reset();
  {
    std::lock_guard lk(jobMu_);
    stopWorker_ = true;
  }
  jobCv_.notify_all();
  if (worker_.joinable()) worker_.join();
  hotkeys_.reset();
  tray_.reset();
  toast_.reset();
  if (hwnd_) DestroyWindow(hwnd_);
}

int Controller::run() {
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return 0;
}

LRESULT CALLBACK Controller::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
  }
  auto* self = reinterpret_cast<Controller*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self && self->hwnd_ == hwnd) return self->handle(msg, wp, lp);
  return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Controller::handle(UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == kRunMsg) {
    std::unique_ptr<std::function<void()>> fn(reinterpret_cast<std::function<void()>*>(lp));
    (*fn)();
    return 0;
  }
  if (msg == WM_HOTKEY) {
    switch (HotkeyManager::fromId(wp).value_or(Action(0))) {
      case Action::SaveClip: saveClip(); break;
      case Action::ToggleRecord: toggleRecord(); break;
      case Action::OpenGallery: openFolder(); break;
    }
    return 0;
  }
  if (msg == TrayIcon::kCallbackMsg) {
    const UINT ev = LOWORD(lp);
    if (ev == WM_CONTEXTMENU || ev == NIN_SELECT || ev == NIN_KEYSELECT) runMenu();
    return 0;
  }
  if (msg == taskbarCreatedMsg_ && taskbarCreatedMsg_ != 0) {
    if (tray_) tray_->recreate();
    return 0;
  }
  return DefWindowProcW(hwnd_, msg, wp, lp);
}

void Controller::runMenu() {
  switch (tray_->showMenu(recorder_ != nullptr, paused_, settings_.hotkeys)) {
    case CmdSave: saveClip(); break;
    case CmdRecord: toggleRecord(); break;
    case CmdPause: togglePause(); break;
    case CmdOpenFolder:
    case CmdGallery: openFolder(); break;
    case CmdSettings: openSettings(); break;
    case CmdReload: reloadSettings(); break;
    case CmdQuit: PostQuitMessage(0); break;
    default: break;
  }
}

void Controller::post(std::function<void()> fn) {
  auto* p = new std::function<void()>(std::move(fn));
  if (!PostMessageW(hwnd_, kRunMsg, 0, reinterpret_cast<LPARAM>(p))) delete p;
}

void Controller::enqueue(std::function<void()> job) {
  {
    std::lock_guard lk(jobMu_);
    jobs_.push_back(std::move(job));
  }
  jobCv_.notify_all();
}

void Controller::workerLoop() {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  while (true) {
    std::function<void()> job;
    {
      std::unique_lock lk(jobMu_);
      jobCv_.wait(lk, [&] { return stopWorker_ || !jobs_.empty(); });
      if (jobs_.empty()) break;
      job = std::move(jobs_.front());
      jobs_.pop_front();
    }
    job();
  }
  CoUninitialize();
}

void Controller::notify(const std::wstring& title, const std::wstring& body, ToastKind kind) {
  if (toast_) toast_->show(title, body, settings_.toastCorner, kind);
}

void Controller::warnAsync(const std::string& msg) {
  logWarn("{}", msg);
  post([this, msg] { notify(L"Clipper", w(msg), ToastKind::Error); });
}

void Controller::updateTray() {
  if (!tray_) return;
  if (recorder_) tray_->setState(TrayState::Recording, L"Clipper · registrazione in corso");
  else if (paused_) tray_->setState(TrayState::Paused, L"Clipper · in pausa");
  else if (!session_) tray_->setState(TrayState::Error, L"Clipper · cattura non attiva");
  else tray_->setState(TrayState::Buffering, w(std::format("Clipper · ultimi {} in memoria", durationText(seconds(settings_.replaySeconds)))));
}

void Controller::applyHotkeys() {
  const auto errors = hotkeys_->apply(settings_.hotkeys);
  std::string text;
  for (const auto& [action, err] : errors) {
    logWarn("{}", err);
    text += (text.empty() ? "" : "\n") + err;
  }
  if (!text.empty()) notify(L"Scorciatoie da controllare", w(text), ToastKind::Error);
}

void Controller::startSession() {
  try {
    session_ = std::make_unique<CaptureSession>(settings_, [this](const std::string& m) { warnAsync(m); });
    paused_ = false;
    logInfo("cattura avviata: {} fps, codec {}, buffer {} s", settings_.fps, int(settings_.codec), settings_.replaySeconds);
  } catch (const std::exception& e) {
    session_.reset();
    logError("avvio cattura fallito: {}", e.what());
    notify(L"Cattura non avviata", w(e.what()), ToastKind::Error);
  } catch (const winrt::hresult_error& e) {
    session_.reset();
    logError("avvio cattura fallito: {}", winrt::to_string(e.message()));
    notify(L"Cattura non avviata", e.message().c_str(), ToastKind::Error);
  }
  updateTray();
}

void Controller::saveClip() {
  if (!session_) {
    notify(L"Cattura in pausa", L"Riprendi la cattura dal menu per salvare clip", ToastKind::Error);
    return;
  }
  auto snap = session_->snapshot(seconds(settings_.replaySeconds));
  if (snap.empty()) {
    notify(L"Buffer vuoto", L"Aspetta qualche secondo e riprova", ToastKind::Error);
    return;
  }
  const std::string app = foregroundAppName();
  const auto when = std::chrono::system_clock::now();
  const Settings s = settings_;
  enqueue([this, snap = std::move(snap), app, when, s] {
    const Ticks t0 = now();
    try {
      // Path resolved here, sequentially, so rapid saves never collide.
      const auto path = uniquePath(outputDir(s), sanitizeFileName(expandNameTemplate(s.nameTemplate, app, when)), "." + s.container);
      writeSnapshot(snap, path, s.container);
      const auto size = fs::file_size(path);
      const Ticks length = snap.end - snap.start;
      logInfo("clip salvata: {} ({}, {}) in {} ms", pathToUtf8(path), durationText(length), formatBytes(size), (now() - t0) / 10'000);
      post([this, length, size, sounds = s.sounds] {
        notify(L"Clip salvata", w(std::format("{} · {}", durationText(length), formatBytes(size))));
        if (sounds) playSaveSound();
      });
    } catch (const std::exception& e) {
      logError("salvataggio clip fallito: {}", e.what());
      post([this, msg = std::string(e.what())] { notify(L"Salvataggio fallito", w(msg), ToastKind::Error); });
    }
  });
}

void Controller::toggleRecord() {
  if (recorder_) {
    std::shared_ptr<Recorder> rec(std::move(recorder_));
    rec->detach();  // on the UI thread: the session (and its bus) may be gone when the worker finalizes
    if (session_) session_->hub().unpin();
    updateTray();
    notify(L"Registrazione fermata", L"Salvataggio in corso…", ToastKind::Recording);
    enqueue([this, rec, sounds = settings_.sounds] {
      try {
        const Ticks length = rec->duration();
        const auto path = rec->stop();
        const auto size = fs::file_size(path);
        logInfo("registrazione salvata: {} ({}, {})", pathToUtf8(path), durationText(length), formatBytes(size));
        post([this, length, size, sounds] {
          notify(L"Registrazione salvata", w(std::format("{} · {}", durationText(length), formatBytes(size))));
          if (sounds) playSaveSound();
        });
      } catch (const std::exception& e) {
        logError("registrazione fallita: {}", e.what());
        post([this, msg = std::string(e.what())] { notify(L"Registrazione fallita", w(msg), ToastKind::Error); });
      }
    });
    return;
  }
  if (!session_) {
    notify(L"Cattura in pausa", L"Riprendi la cattura dal menu per registrare", ToastKind::Error);
    return;
  }
  try {
    session_->hub().pin();
    const auto stem = sanitizeFileName(expandNameTemplate(settings_.nameTemplate, foregroundAppName(), std::chrono::system_clock::now()));
    const auto path = uniquePath(outputDir(settings_), stem, "." + settings_.container);
    recorder_ = std::make_unique<Recorder>(session_->bus(), session_->tracks(), path, settings_.container,
                                           [this] { session_->requestKeyframe(); });
    logInfo("registrazione avviata: {}", pathToUtf8(path));
    notify(L"Registrazione avviata", w(std::format("Premi {} per fermare", settings_.hotkeys.toggleRecord)), ToastKind::Recording);
  } catch (const std::exception& e) {
    session_->hub().unpin();
    recorder_.reset();
    logError("avvio registrazione fallito: {}", e.what());
    notify(L"Registrazione non avviata", w(e.what()), ToastKind::Error);
  }
  updateTray();
}

void Controller::togglePause() {
  if (recorder_) return;
  if (session_) {
    session_.reset();
    paused_ = true;
    logInfo("cattura in pausa");
    notify(L"Cattura in pausa", L"Nessuna risorsa in uso finché non riprendi");
    updateTray();
  } else {
    startSession();
    if (session_) notify(L"Cattura ripresa", L"Buffer di nuovo attivo");
  }
}

void Controller::reloadSettings() {
  if (recorder_) {
    notify(L"Registrazione in corso", L"Ferma la registrazione prima di ricaricare le impostazioni", ToastKind::Error);
    return;
  }
  settings_ = loadSettings(settingsPath_);
  applyHotkeys();
  const bool wasPaused = paused_;
  session_.reset();
  if (!wasPaused) startSession();
  logInfo("impostazioni ricaricate");
  if (session_ || wasPaused) notify(L"Impostazioni ricaricate", w(std::format("Buffer: ultimi {}", durationText(seconds(settings_.replaySeconds)))));
}

void Controller::openSettings() {
  if (!fs::exists(settingsPath_)) saveSettings(settings_, settingsPath_);
  const std::wstring args = L"\"" + settingsPath_.wstring() + L"\"";
  ShellExecuteW(nullptr, L"open", L"notepad.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
  notify(L"Impostazioni", L"Salva il file, poi scegli \"Ricarica impostazioni\" dal menu");
}

void Controller::openFolder() {
  ShellExecuteW(nullptr, L"open", outputDir(settings_).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace clipper
