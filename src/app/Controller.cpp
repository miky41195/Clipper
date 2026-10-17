#include "app/Controller.h"
#include "app/ClipNaming.h"
#include "app/SettingsDiff.h"
#include "app/Startup.h"
#include "audio/Wasapi.h"
#include "core/Log.h"
#include "platform/Autostart.h"
#include "platform/FolderPicker.h"
#include "ui/Color.h"
#include "core/I18n.h"
#include "ui/SettingsModel.h"
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
std::wstring T(const char* key) { return utf8ToWide(tr(key)); }

void playSaveSound() { PlaySoundW(L"SystemNotification", nullptr, SND_ALIAS | SND_ASYNC | SND_NODEFAULT); }

// Folders come from settings.json: "explore" on a checked directory never runs a file put there instead.
void exploreFolder(const fs::path& dir) {
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return;
  ShellExecuteW(nullptr, L"explore", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::string durationText(Ticks t) {
  const auto secs = int((t + kTicksPerSecond / 2) / kTicksPerSecond);
  return secs >= 60 ? std::format("{}:{:02} min", secs / 60, secs % 60) : std::format("{} s", secs);
}
}  // namespace

Controller::Controller(HINSTANCE inst, bool showUi) : inst_(inst) {
  settingsPath_ = defaultSettingsPath();
  settings_ = loadSettings(settingsPath_);
  if (!fs::exists(settingsPath_)) saveSettings(settings_, settingsPath_);
  setLanguage(settings_.language);

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
  buildUi();
  if (showUi) openUi(true);
  if (session_)
    notify(T("toast.started"),
           w(trf("toast.started.body", durationLabel(settings_.replaySeconds), settings_.hotkeys.saveClip)));
}

Controller::~Controller() {
  ui_.reset();
  app_.reset();
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
  while (true) {
    if (ui_ && ui_->needsFrames()) {
      // UI open and something changed: drain input, then draw one frame (vsync paced).
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return 0;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
      ui_->renderFrame();
    } else {
      // Idle: block, no CPU used.
      if (GetMessageW(&msg, nullptr, 0, 0) <= 0) return 0;
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
}

void Controller::buildUi() {
  UiContext& c = uiCtx_;
  c.currentSettings = [this] { return settings_; };
  c.applySettings = [this](const Settings& s) { return applyFromUi(s); };
  c.monitors = [] { return listMonitors(); };
  c.microphones = [] { return listMicrophones(); };
  c.liveAudioApps = [this] {
    auto groups = groupAudioApps(audioSessionPids(true), snapshotProcesses(), {}, GetCurrentProcessId());
    for (const auto& g : groups) audioPids_[g.exe] = g.roots.front();
    return groups;
  };
  c.appName = [this](const std::string& exe) {
    const auto it = audioPids_.find(exe);
    return appDisplayName(it == audioPids_.end() ? 0 : it->second, exe);
  };
  c.suspendHotkeys = [this](bool suspend) {
    if (suspend) hotkeys_->apply(HotkeySettings{"", "", ""});
    else hotkeys_->apply(settings_.hotkeys);
  };
  c.status = [this] {
    if (recorder_) return std::pair{AppState::Recording, std::string(tr("status.recording"))};
    if (paused_) return std::pair{AppState::Paused, std::string(tr("status.paused"))};
    if (!session_) return std::pair{AppState::Off, std::string(tr("status.off"))};
    return std::pair{AppState::Buffering, trf("status.buffering", durationLabel(settings_.replaySeconds))};
  };
  c.clipsFolder = [](const Settings& s) { return outputDir(s); };
  c.bufferFolder = [](const Settings& s) { return bufferDir(s); };
  c.openFolder = [](const fs::path& p) { exploreFolder(p); };
  c.pickFolder = [this](const fs::path& initial) { return pickFolder(ui_ ? ui_->hwnd() : hwnd_, initial); };
  c.autostart = [] { return autostartEnabled(); };
  c.setAutostart = [](bool on) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    setAutostart(on, exe);
  };
  c.thumbnailCache = [] { return localDataDir() / L"thumbs"; };
  app_ = std::make_unique<AppUi>(uiCtx_);
  ui_ = std::make_unique<UiWindow>(inst_, [this](const UiFonts& f) {
    app_->draw(f);
    ui_->setAnimating(app_->wantsAnimation());  // playback, export, thumbnails: keep drawing
  });
  ui_->onClosing = [this] { app_->onClose(); };
  ui_->deviceProvider = [this] { return sharedGpu(); };
  ui_->setAccent(parseHexColor(settings_.accent).value_or(IM_COL32(61, 220, 132, 255)));
}

void Controller::openUi(bool gallery) {
  if (gallery) app_->showGallery();
  else app_->showSettings(SettingsSection::Capture);
  const bool wasOpen = ui_->open();
  try {
    ui_->show();
  } catch (const winrt::hresult_error& e) {
    logError("apertura finestra fallita: {}", winrt::to_string(e.message()));
    ui_->close();
    notify(T("toast.window"), e.message().c_str(), ToastKind::Error);
    return;
  }
  if (!wasOpen) app_->onOpen();
}

std::string Controller::applyFromUi(const Settings& s) {
  const bool restart = captureChanged(settings_, s);
  if (restart && recorder_) return tr("apply.recording");
  const auto errors = hotkeys_->apply(s.hotkeys);
  if (!errors.empty()) {
    hotkeys_->apply(settings_.hotkeys);  // keep the previous working shortcuts
    std::string text;
    for (const auto& [action, err] : errors) text += (text.empty() ? "" : " · ") + err;
    return text;
  }
  const Settings old = settings_;
  settings_ = s;
  try {
    saveSettings(settings_, settingsPath_);
  } catch (const std::exception& e) {
    logError("salvataggio impostazioni fallito: {}", e.what());
  }
  setLanguage(settings_.language);
  if (const auto c = parseHexColor(settings_.accent)) ui_->setAccent(*c);
  logInfo("impostazioni applicate (riavvio cattura: {})", restart);
  if (restart && !paused_) {
    session_.reset();
    startSession();
  }
  updateTray();
  return {};
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
  if (msg == WM_TIMER && wp == kSaveRetryTimer) {
    KillTimer(hwnd_, kSaveRetryTimer);
    saveClip(saveAttempt_);
    return 0;
  }
  if (msg == WM_HOTKEY) {
    switch (HotkeyManager::fromId(wp).value_or(Action(0))) {
      case Action::SaveClip: saveClip(); break;
      case Action::ToggleRecord: toggleRecord(); break;
      case Action::OpenGallery: openUi(true); break;
    }
    return 0;
  }
  if (msg == TrayIcon::kCallbackMsg) {
    const UINT ev = LOWORD(lp);
    if (ev == WM_CONTEXTMENU || ev == NIN_SELECT || ev == NIN_KEYSELECT) runMenu();
    return 0;
  }
  if (msg == WM_APP + 3) {  // clipper.exe launched again: show the window
    openUi(true);
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
    case CmdOpenFolder: openFolder(); break;
    case CmdGallery: openUi(true); break;
    case CmdSettings: openUi(false); break;
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
  if (ui_) ui_->requestFrames();  // status pill in the open window
  if (!tray_) return;
  if (recorder_) tray_->setState(TrayState::Recording, T("tray.tip.recording"));
  else if (paused_) tray_->setState(TrayState::Paused, T("tray.tip.paused"));
  else if (!session_) tray_->setState(TrayState::Error, T("tray.tip.off"));
  else tray_->setState(TrayState::Buffering, w(trf("tray.tip.buffering", durationLabel(settings_.replaySeconds))));
}

void Controller::applyHotkeys() {
  const auto errors = hotkeys_->apply(settings_.hotkeys);
  std::string text;
  for (const auto& [action, err] : errors) {
    logWarn("{}", err);
    text += (text.empty() ? "" : "\n") + err;
  }
  if (!text.empty()) notify(T("toast.hotkeys"), w(text), ToastKind::Error);
}

void Controller::startSession() {
  try {
    session_ = std::make_unique<CaptureSession>(settings_, [this](const std::string& m) { warnAsync(m); }, sharedGpu());
    paused_ = false;
    logInfo("cattura avviata: {} fps, codec {}, buffer {} s", settings_.fps, int(settings_.codec), settings_.replaySeconds);
    // On a GPU without the chosen codec (e.g. AV1 on older cards) the encoder fell back: remember the codec
    // that works so the warning appears only once.
    for (const auto& t : session_->tracks())
      if (t.kind == TrackKind::Video && t.codec != settings_.codec) {
        logInfo("codec non supportato dalla GPU: passo a {}", int(t.codec));
        settings_.codec = t.codec;
        try {
          saveSettings(settings_, settingsPath_);
        } catch (...) {
        }
      }
  } catch (const std::exception& e) {
    session_.reset();
    logError("avvio cattura fallito: {}", e.what());
    notify(T("toast.capture.failed"), w(e.what()), ToastKind::Error);
  } catch (const winrt::hresult_error& e) {
    session_.reset();
    logError("avvio cattura fallito: {}", winrt::to_string(e.message()));
    notify(T("toast.capture.failed"), e.message().c_str(), ToastKind::Error);
  }
  updateTray();
}

void Controller::saveClip(int attempt) {
  if (attempt == 0) logInfo("salvataggio clip richiesto");
  if (!session_) {
    notify(T("toast.paused"), T("toast.paused.save"), ToastKind::Error);
    return;
  }
  auto snap = session_->snapshot(seconds(settings_.replaySeconds));
  const bool tooShort = snap.empty() || snap.end - snap.start < seconds(std::min(2, settings_.replaySeconds));
  if (tooShort && attempt < kSaveRetries) {
    // Capture just started: give the buffer a moment, then save what there is (a shorter clip).
    if (attempt == 0) notify(T("toast.starting"), T("toast.starting.body"));
    saveAttempt_ = attempt + 1;
    SetTimer(hwnd_, kSaveRetryTimer, 250, nullptr);
    return;
  }
  if (snap.empty()) {
    logWarn("salvataggio clip: buffer vuoto dopo {} tentativi", attempt);
    notify(T("toast.empty"), T("toast.empty.body"), ToastKind::Error);
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
      writeSnapshot(snap, path, s.container, app);
      const auto size = fs::file_size(path);
      const Ticks length = snap.end - snap.start;
      logInfo("clip salvata: {} ({}, {}) in {} ms", pathToUtf8(path), durationText(length), formatBytes(size), (now() - t0) / 10'000);
      const bool partial = length < seconds(s.replaySeconds - 1);
      post([this, length, size, partial, sounds = s.sounds] {
        if (ui_ && ui_->open()) { app_->clipsChanged(); ui_->requestFrames(); }
        notify(T("toast.saved"), w(std::format("{} · {}{}", durationText(length), formatBytes(size), partial ? tr("toast.partial") : "")));
        if (sounds) playSaveSound();
      });
    } catch (const std::exception& e) {
      logError("salvataggio clip fallito: {}", e.what());
      post([this, msg = std::string(e.what())] { notify(T("toast.savefail"), w(msg), ToastKind::Error); });
    }
  });
}

void Controller::toggleRecord() {
  if (recorder_) {
    std::shared_ptr<Recorder> rec(std::move(recorder_));
    rec->detach();  // on the UI thread: the session (and its bus) may be gone when the worker finalizes
    if (session_) session_->hub().unpin();
    updateTray();
    notify(T("toast.rec.stopped"), T("toast.rec.saving"), ToastKind::Recording);
    enqueue([this, rec, sounds = settings_.sounds] {
      try {
        const Ticks length = rec->duration();
        const auto path = rec->stop();
        const auto size = fs::file_size(path);
        logInfo("registrazione salvata: {} ({}, {})", pathToUtf8(path), durationText(length), formatBytes(size));
        post([this, length, size, sounds] {
          if (ui_ && ui_->open()) { app_->clipsChanged(); ui_->requestFrames(); }
          notify(T("toast.rec.saved"), w(std::format("{} · {}", durationText(length), formatBytes(size))));
          if (sounds) playSaveSound();
        });
      } catch (const std::exception& e) {
        logError("registrazione fallita: {}", e.what());
        post([this, msg = std::string(e.what())] { notify(T("toast.rec.failed"), w(msg), ToastKind::Error); });
      }
    });
    return;
  }
  if (!session_) {
    notify(T("toast.paused"), T("toast.paused.rec"), ToastKind::Error);
    return;
  }
  try {
    session_->hub().pin();
    const std::string app = foregroundAppName();
    const auto stem = sanitizeFileName(expandNameTemplate(settings_.nameTemplate, app, std::chrono::system_clock::now()));
    const auto path = uniquePath(outputDir(settings_), stem, "." + settings_.container);
    recorder_ = std::make_unique<Recorder>(session_->bus(), session_->tracks(), path, settings_.container,
                                           [this] { session_->requestKeyframe(); }, app);
    logInfo("registrazione avviata: {}", pathToUtf8(path));
    notify(T("toast.rec.started"), w(trf("toast.rec.started.body", settings_.hotkeys.toggleRecord)), ToastKind::Recording);
  } catch (const std::exception& e) {
    session_->hub().unpin();
    recorder_.reset();
    logError("avvio registrazione fallito: {}", e.what());
    notify(T("toast.rec.notstarted"), w(e.what()), ToastKind::Error);
  }
  updateTray();
}

void Controller::togglePause() {
  if (recorder_) return;
  if (session_) {
    session_.reset();
    paused_ = true;
    logInfo("cattura in pausa");
    notify(T("toast.paused"), T("toast.paused.body"));
    updateTray();
  } else {
    startSession();
    if (session_) notify(T("toast.resumed"), T("toast.resumed.body"));
  }
}

void Controller::openFolder() {
  exploreFolder(outputDir(settings_));
}

GpuDevice Controller::sharedGpu() {
  // Recreate after a driver reset (TDR); otherwise reuse so the window adds no second device.
  if (!gpu_.device || gpu_.device->GetDeviceRemovedReason() != S_OK) {
    try {
      gpu_ = createGpuDevice();
    } catch (const winrt::hresult_error& e) {
      logError("creazione device GPU fallita: {}", winrt::to_string(e.message()));
      gpu_ = {};
    }
  }
  return gpu_;
}

}  // namespace clipper

