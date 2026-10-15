#include "audio/Wasapi.h"
#include "core/Text.h"
#include <windows.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <audiopolicy.h>
#include <initguid.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <tlhelp32.h>
#include <future>
#include <stdexcept>
#include <winrt/base.h>

namespace clipper {
using winrt::check_hresult;
using winrt::com_ptr;

namespace {

struct ActivationHandler : winrt::implements<ActivationHandler, IActivateAudioInterfaceCompletionHandler> {
  winrt::handle done{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
  HRESULT hr = E_FAIL;
  com_ptr<IAudioClient> client;
  HRESULT __stdcall ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op) noexcept override {
    HRESULT ahr = E_FAIL;
    com_ptr<IUnknown> unk;
    hr = op->GetActivateResult(&ahr, unk.put());
    if (SUCCEEDED(hr)) hr = ahr;
    if (SUCCEEDED(hr)) client = unk.try_as<IAudioClient>();
    if (SUCCEEDED(hr) && !client) hr = E_NOINTERFACE;
    SetEvent(done.get());
    return S_OK;
  }
};

IAudioClient* activateProcessLoopback(uint32_t pid) {
  AUDIOCLIENT_ACTIVATION_PARAMS p{};
  p.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
  p.ProcessLoopbackParams.TargetProcessId = pid;
  p.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
  PROPVARIANT pv{};
  pv.vt = VT_BLOB;
  pv.blob.cbSize = sizeof(p);
  pv.blob.pBlobData = reinterpret_cast<BYTE*>(&p);
  auto h = winrt::make_self<ActivationHandler>();
  com_ptr<IActivateAudioInterfaceAsyncOperation> op;
  check_hresult(ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &pv, h.get(),
                                            op.put()));
  if (WaitForSingleObject(h->done.get(), 5000) != WAIT_OBJECT_0) throw std::runtime_error("attivazione audio scaduta");
  check_hresult(h->hr);
  return h->client.detach();
}

IAudioClient* activateMic(const std::string& id) {
  auto en = winrt::create_instance<IMMDeviceEnumerator>(__uuidof(MMDeviceEnumerator));
  com_ptr<IMMDevice> dev;
  if (id.empty()) check_hresult(en->GetDefaultAudioEndpoint(eCapture, eConsole, dev.put()));
  else check_hresult(en->GetDevice(utf8ToWide(id).c_str(), dev.put()));
  IAudioClient* client = nullptr;
  check_hresult(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client)));
  return client;
}

}  // namespace

WasapiCapture::~WasapiCapture() {
  stop_ = true;
  if (thread_.joinable()) thread_.join();
}

void WasapiCapture::start(std::function<void*()> activate, bool loopback, std::shared_ptr<PcmSource> sink) {
  sink_ = std::move(sink);
  std::promise<void> ready;
  auto readyFuture = ready.get_future();
  thread_ = std::thread([this, activate = std::move(activate), loopback, ready = std::move(ready)]() mutable {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    com_ptr<IAudioClient> client;
    com_ptr<IAudioCaptureClient> cap;
    try {
      client.attach(static_cast<IAudioClient*>(activate()));
      WAVEFORMATEX wf{WAVE_FORMAT_PCM, 2, 48000, 48000 * 4, 4, 16, 0};
      DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                    AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
      if (loopback) flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
      check_hresult(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 200'000, 0, &wf, nullptr));
      check_hresult(client->SetEventHandle(event));
      check_hresult(client->GetService(IID_PPV_ARGS(cap.put())));
      check_hresult(client->Start());
      ready.set_value();
    } catch (...) {
      failed_ = true;
      ready.set_exception(std::current_exception());
      CloseHandle(event);
      CoUninitialize();
      return;
    }
    std::vector<int16_t> silence;
    while (!stop_) {
      if (WaitForSingleObject(event, 100) != WAIT_OBJECT_0) continue;
      UINT32 next = 0;
      while (SUCCEEDED(cap->GetNextPacketSize(&next)) && next > 0) {
        BYTE* data = nullptr;
        UINT32 frames = 0;
        DWORD fl = 0;
        UINT64 devPos = 0, qpc = 0;
        const HRESULT hr = cap->GetBuffer(&data, &frames, &fl, &devPos, &qpc);
        if (FAILED(hr)) {
          failed_ = true;
          break;
        }
        // qpc is already in 100 ns units on the QueryPerformanceCounter clock (same as now()).
        const Ticks ts = qpc ? Ticks(qpc) : now() - Ticks(frames) * kTicksPerSecond / 48000;
        if (fl & AUDCLNT_BUFFERFLAGS_SILENT) {
          silence.assign(size_t(frames) * 2, 0);
          sink_->push(silence.data(), frames, ts);
        } else {
          sink_->push(reinterpret_cast<const int16_t*>(data), frames, ts);
        }
        cap->ReleaseBuffer(frames);
      }
      if (failed_) break;
    }
    client->Stop();
    cap = nullptr;
    client = nullptr;
    CloseHandle(event);
    CoUninitialize();
  });
  readyFuture.get();  // rethrows initialization errors
}

LoopbackCapture::LoopbackCapture(uint32_t pid, std::shared_ptr<PcmSource> sink) {
  start([pid] { return static_cast<void*>(activateProcessLoopback(pid)); }, true, std::move(sink));
}

MicCapture::MicCapture(const std::string& deviceId, std::shared_ptr<PcmSource> sink) {
  start([deviceId] { return static_cast<void*>(activateMic(deviceId)); }, false, std::move(sink));
}

std::vector<std::pair<std::string, std::string>> listMicrophones() {
  std::vector<std::pair<std::string, std::string>> out;
  auto en = winrt::create_instance<IMMDeviceEnumerator>(__uuidof(MMDeviceEnumerator));
  com_ptr<IMMDeviceCollection> col;
  if (FAILED(en->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, col.put()))) return out;
  UINT n = 0;
  col->GetCount(&n);
  for (UINT i = 0; i < n; ++i) {
    com_ptr<IMMDevice> dev;
    if (FAILED(col->Item(i, dev.put()))) continue;
    LPWSTR id = nullptr;
    dev->GetId(&id);
    com_ptr<IPropertyStore> props;
    std::string name;
    if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, props.put()))) {
      PROPVARIANT v;
      PropVariantInit(&v);
      if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) name = wideToUtf8(v.pwszVal);
      PropVariantClear(&v);
    }
    out.emplace_back(id ? wideToUtf8(id) : std::string{}, name);
    CoTaskMemFree(id);
  }
  return out;
}

std::vector<uint32_t> audioSessionPids() {
  std::vector<uint32_t> out;
  auto en = winrt::create_instance<IMMDeviceEnumerator>(__uuidof(MMDeviceEnumerator));
  com_ptr<IMMDeviceCollection> col;
  if (FAILED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, col.put()))) return out;
  UINT n = 0;
  col->GetCount(&n);
  for (UINT i = 0; i < n; ++i) {
    com_ptr<IMMDevice> dev;
    com_ptr<IAudioSessionManager2> mgr;
    com_ptr<IAudioSessionEnumerator> se;
    if (FAILED(col->Item(i, dev.put())) ||
        FAILED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, mgr.put_void())) ||
        FAILED(mgr->GetSessionEnumerator(se.put())))
      continue;
    int count = 0;
    se->GetCount(&count);
    for (int k = 0; k < count; ++k) {
      com_ptr<IAudioSessionControl> ctl;
      if (FAILED(se->GetSession(k, ctl.put()))) continue;
      auto ctl2 = ctl.try_as<IAudioSessionControl2>();
      if (!ctl2 || ctl2->IsSystemSoundsSession() == S_OK) continue;
      AudioSessionState state;
      if (FAILED(ctl2->GetState(&state)) || state == AudioSessionStateExpired) continue;
      DWORD pid = 0;
      if (SUCCEEDED(ctl2->GetProcessId(&pid)) && pid != 0) out.push_back(pid);
    }
  }
  return out;
}

std::vector<ProcEntry> snapshotProcesses() {
  std::vector<ProcEntry> out;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return out;
  PROCESSENTRY32W pe{sizeof(pe)};
  for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
    out.push_back({pe.th32ProcessID, pe.th32ParentProcessID, toLowerAscii(wideToUtf8(pe.szExeFile))});
  CloseHandle(snap);
  return out;
}

std::string appDisplayName(uint32_t pid, const std::string& exe) {
  if (pid != 0) {
    if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
      wchar_t path[MAX_PATH * 2];
      DWORD len = DWORD(std::size(path));
      std::string name;
      if (QueryFullProcessImageNameW(h, 0, path, &len)) {
        DWORD dummy = 0;
        const DWORD size = GetFileVersionInfoSizeW(path, &dummy);
        if (size > 0) {
          std::vector<BYTE> data(size);
          struct Lang {
            WORD lang, cp;
          }* langs = nullptr;
          UINT langLen = 0;
          if (GetFileVersionInfoW(path, 0, size, data.data()) &&
              VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&langs), &langLen) &&
              langLen >= sizeof(Lang)) {
            wchar_t key[64];
            swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\FileDescription", langs[0].lang, langs[0].cp);
            wchar_t* desc = nullptr;
            UINT descLen = 0;
            if (VerQueryValueW(data.data(), key, reinterpret_cast<void**>(&desc), &descLen) && descLen > 1)
              name = wideToUtf8(desc);
          }
        }
      }
      CloseHandle(h);
      while (!name.empty() && name.back() == ' ') name.pop_back();
      if (!name.empty()) return name;
    }
  }
  std::string stem = exe;
  if (const auto dot = stem.rfind('.'); dot != std::string::npos) stem.resize(dot);
  if (!stem.empty() && stem[0] >= 'a' && stem[0] <= 'z') stem[0] = char(stem[0] - 'a' + 'A');
  return stem.empty() ? "App" : stem;
}

}  // namespace clipper
