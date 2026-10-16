#include "gallery/AudioOut.h"
#include <windows.h>
#include <audioclient.h>
#include <ks.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <future>
#include <winrt/base.h>

namespace clipper {
using winrt::check_hresult;
using winrt::com_ptr;

AudioOut::AudioOut(Fill fill) : fill_(std::move(fill)) {
  std::promise<void> ready;
  auto readyFuture = ready.get_future();
  thread_ = std::thread([this, ready = std::move(ready)]() mutable {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    com_ptr<IAudioClient> client;
    com_ptr<IAudioRenderClient> render;
    UINT32 bufferFrames = 0;
    try {
      auto en = winrt::create_instance<IMMDeviceEnumerator>(__uuidof(MMDeviceEnumerator));
      com_ptr<IMMDevice> dev;
      check_hresult(en->GetDefaultAudioEndpoint(eRender, eConsole, dev.put()));
      check_hresult(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, client.put_void()));
      WAVEFORMATEXTENSIBLE wf{};
      wf.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
      wf.Format.nChannels = 2;
      wf.Format.nSamplesPerSec = 48000;
      wf.Format.wBitsPerSample = 32;
      wf.Format.nBlockAlign = 8;
      wf.Format.nAvgBytesPerSec = 48000 * 8;
      wf.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
      wf.Samples.wValidBitsPerSample = 32;
      wf.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
      wf.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
      check_hresult(client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                       AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                           AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                       400'000, 0, &wf.Format, nullptr));
      check_hresult(client->SetEventHandle(event));
      check_hresult(client->GetBufferSize(&bufferFrames));
      check_hresult(client->GetService(IID_PPV_ARGS(render.put())));
      BYTE* data = nullptr;
      if (SUCCEEDED(render->GetBuffer(bufferFrames, &data))) render->ReleaseBuffer(bufferFrames, AUDCLNT_BUFFERFLAGS_SILENT);
      check_hresult(client->Start());
      ready.set_value();
    } catch (...) {
      ready.set_exception(std::current_exception());
      CloseHandle(event);
      CoUninitialize();
      return;
    }
    while (!stop_) {
      if (WaitForSingleObject(event, 100) != WAIT_OBJECT_0) continue;
      UINT32 padding = 0;
      if (FAILED(client->GetCurrentPadding(&padding))) break;
      const UINT32 frames = bufferFrames - padding;
      if (frames == 0) continue;
      BYTE* data = nullptr;
      if (FAILED(render->GetBuffer(frames, &data))) break;
      fill_(reinterpret_cast<float*>(data), frames);
      render->ReleaseBuffer(frames, 0);
    }
    client->Stop();
    render = nullptr;
    client = nullptr;
    CloseHandle(event);
    CoUninitialize();
  });
  try {
    readyFuture.get();
  } catch (...) {
    thread_.join();
    throw;
  }
}

AudioOut::~AudioOut() {
  stop_ = true;
  if (thread_.joinable()) thread_.join();
}

}  // namespace clipper
