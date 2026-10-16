#include "encode/VideoEncoder.h"
#include "core/I18n.h"
#include "core/Text.h"
#include <codecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <strmif.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <format>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace clipper {
using winrt::check_hresult;
using winrt::com_ptr;

std::string codecName(Codec c) {
  switch (c) {
    case Codec::H264: return "H.264";
    case Codec::HEVC: return "HEVC";
    case Codec::AV1: return "AV1";
    case Codec::AAC: return "AAC";
  }
  return "?";
}

namespace {

GUID subtypeFor(Codec c) {
  switch (c) {
    case Codec::HEVC: return MFVideoFormat_HEVC;
    case Codec::AV1: return MFVideoFormat_AV1;
    default: return MFVideoFormat_H264;
  }
}

// Scans Annex-B NAL units until the first VCL unit decides.
bool annexBKey(const uint8_t* d, size_t n, bool hevc) {
  for (size_t i = 0; i + 3 < n; ++i) {
    if (d[i] != 0 || d[i + 1] != 0 || d[i + 2] != 1) continue;
    const uint8_t h = d[i + 3];
    if (!hevc) {
      const int t = h & 0x1F;
      if (t == 5) return true;
      if (t == 1) return false;
    } else {
      const int t = (h >> 1) & 0x3F;
      if (t >= 16 && t <= 21) return true;
      if (t <= 9) return false;
    }
    i += 2;
  }
  return false;
}

bool av1Key(const uint8_t* d, size_t n) {
  size_t pos = 0;
  while (pos < n) {
    const uint8_t h = d[pos];
    const int type = (h >> 3) & 0x0F;
    const bool ext = (h >> 2) & 1;
    const bool hasSize = (h >> 1) & 1;
    if (type == 1) return true;  // sequence header precedes every keyframe
    if (type == 6 || type == 3) return false;
    pos += 1 + (ext ? 1 : 0);
    if (!hasSize) return false;
    uint64_t size = 0;
    for (int k = 0; k < 8 && pos < n; ++k) {
      const uint8_t b = d[pos++];
      size |= uint64_t(b & 0x7F) << (7 * k);
      if (!(b & 0x80)) break;
    }
    pos += size_t(size);
  }
  return false;
}

void setProp(ICodecAPI* api, const GUID& prop, UINT32 v) {
  VARIANT var;
  VariantInit(&var);
  var.vt = VT_UI4;
  var.ulVal = v;
  api->SetValue(&prop, &var);
}

void setBool(ICodecAPI* api, const GUID& prop, bool v) {
  VARIANT var;
  VariantInit(&var);
  var.vt = VT_BOOL;
  var.boolVal = v ? VARIANT_TRUE : VARIANT_FALSE;
  api->SetValue(&prop, &var);
}

void setColor(IMFMediaType* t) {
  t->SetUINT32(MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_BT709);
  t->SetUINT32(MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_709);
  t->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
  t->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
}

}  // namespace

bool isKeyframe(Codec codec, const uint8_t* data, size_t size) {
  switch (codec) {
    case Codec::H264: return annexBKey(data, size, false);
    case Codec::HEVC: return annexBKey(data, size, true);
    case Codec::AV1: return av1Key(data, size);
    default: return true;
  }
}

struct VideoEncoder::Impl {
  VideoEncoderConfig cfg;
  PacketSink sink;
  GpuDevice gpu;
  com_ptr<IMFTransform> mft;
  com_ptr<ICodecAPI> codecApi;
  com_ptr<IMFDXGIDeviceManager> manager;
  com_ptr<IMFMediaEventGenerator> events;
  com_ptr<ID3D11Texture2D> staging;
  bool async = false;
  bool hardware = false;
  bool d3dAware = false;
  bool streaming = false;
  std::string name;
  std::mutex mu;
  std::condition_variable cv;
  int needInput = 0;
  bool drained = false;
  bool failed = false;
  std::thread eventThread;
  std::atomic<bool> forceKey{false};
  Ticks frameDuration = 0;

  void init(IMFActivate* act);
  void eventLoop();
  bool pullOutput();
  void emit(IMFSample* s);
  com_ptr<IMFSample> makeSample(ID3D11Texture2D* tex, Ticks pts);
  void shutdown();
};

void VideoEncoder::Impl::init(IMFActivate* act) {
  check_hresult(act->ActivateObject(IID_PPV_ARGS(mft.put())));
  wchar_t* fn = nullptr;
  UINT32 len = 0;
  if (SUCCEEDED(act->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &fn, &len))) {
    name = wideToUtf8(fn);
    CoTaskMemFree(fn);
  }
  com_ptr<IMFAttributes> attrs;
  check_hresult(mft->GetAttributes(attrs.put()));
  UINT32 v = 0;
  async = SUCCEEDED(attrs->GetUINT32(MF_TRANSFORM_ASYNC, &v)) && v;
  if (async) check_hresult(attrs->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE));
  attrs->SetUINT32(MF_LOW_LATENCY, TRUE);
  v = 0;
  d3dAware = SUCCEEDED(attrs->GetUINT32(MF_SA_D3D11_AWARE, &v)) && v;
  if (d3dAware) {
    UINT token = 0;
    check_hresult(MFCreateDXGIDeviceManager(&token, manager.put()));
    check_hresult(manager->ResetDevice(gpu.device.get(), token));
    check_hresult(mft->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(manager.get())));
  }
  codecApi = mft.try_as<ICodecAPI>();
  if (codecApi) {
    setProp(codecApi.get(), CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_CBR);
    setProp(codecApi.get(), CODECAPI_AVEncCommonMeanBitRate, UINT32(cfg.kbps) * 1000);
    setProp(codecApi.get(), CODECAPI_AVEncMPVGOPSize, UINT32(cfg.fps));
    setProp(codecApi.get(), CODECAPI_AVEncMPVDefaultBPictureCount, 0);
    setBool(codecApi.get(), CODECAPI_AVLowLatencyMode, true);
  }

  com_ptr<IMFMediaType> out;
  check_hresult(MFCreateMediaType(out.put()));
  out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  out->SetGUID(MF_MT_SUBTYPE, subtypeFor(cfg.codec));
  out->SetUINT32(MF_MT_AVG_BITRATE, UINT32(cfg.kbps) * 1000);
  MFSetAttributeSize(out.get(), MF_MT_FRAME_SIZE, UINT32(cfg.width), UINT32(cfg.height));
  MFSetAttributeRatio(out.get(), MF_MT_FRAME_RATE, UINT32(cfg.fps), 1);
  MFSetAttributeRatio(out.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
  out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  if (cfg.codec == Codec::H264) out->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);
  if (cfg.codec == Codec::HEVC) out->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH265VProfile_Main_420_8);
  setColor(out.get());
  check_hresult(mft->SetOutputType(0, out.get(), 0));

  com_ptr<IMFMediaType> in;
  check_hresult(MFCreateMediaType(in.put()));
  in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
  MFSetAttributeSize(in.get(), MF_MT_FRAME_SIZE, UINT32(cfg.width), UINT32(cfg.height));
  MFSetAttributeRatio(in.get(), MF_MT_FRAME_RATE, UINT32(cfg.fps), 1);
  MFSetAttributeRatio(in.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
  in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  setColor(in.get());
  check_hresult(mft->SetInputType(0, in.get(), 0));

  if (!d3dAware) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = UINT(cfg.width);
    desc.Height = UINT(cfg.height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    check_hresult(gpu.device->CreateTexture2D(&desc, nullptr, staging.put()));
  }
  check_hresult(mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0));
  check_hresult(mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));
  streaming = true;
  if (async) {
    events = mft.as<IMFMediaEventGenerator>();
    eventThread = std::thread([this] { eventLoop(); });
  }
}

void VideoEncoder::Impl::eventLoop() {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  while (true) {
    com_ptr<IMFMediaEvent> ev;
    if (FAILED(events->GetEvent(0, ev.put()))) break;
    MediaEventType type = MEUnknown;
    ev->GetType(&type);
    HRESULT status = S_OK;
    ev->GetStatus(&status);
    if (FAILED(status)) {
      std::lock_guard lk(mu);
      failed = true;
      cv.notify_all();
      break;
    }
    if (type == METransformNeedInput) {
      std::lock_guard lk(mu);
      ++needInput;
      cv.notify_all();
    } else if (type == METransformHaveOutput) {
      pullOutput();
    } else if (type == METransformDrainComplete) {
      std::lock_guard lk(mu);
      drained = true;
      cv.notify_all();
    }
  }
  {
    std::lock_guard lk(mu);
    failed = true;
    cv.notify_all();
  }
  CoUninitialize();
}

bool VideoEncoder::Impl::pullOutput() {
  MFT_OUTPUT_STREAM_INFO info{};
  if (FAILED(mft->GetOutputStreamInfo(0, &info))) return false;
  com_ptr<IMFSample> own;
  if (!(info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES))) {
    if (FAILED(MFCreateSample(own.put()))) return false;
    com_ptr<IMFMediaBuffer> buf;
    const DWORD size = info.cbSize ? info.cbSize : DWORD(cfg.width * cfg.height * 2);
    if (FAILED(MFCreateMemoryBuffer(size, buf.put()))) return false;
    own->AddBuffer(buf.get());
  }
  MFT_OUTPUT_DATA_BUFFER ob{};
  ob.dwStreamID = 0;
  ob.pSample = own.get();
  DWORD status = 0;
  const HRESULT hr = mft->ProcessOutput(0, 1, &ob, &status);
  if (ob.pEvents) ob.pEvents->Release();
  if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
    com_ptr<IMFMediaType> t;
    if (SUCCEEDED(mft->GetOutputAvailableType(0, 0, t.put()))) mft->SetOutputType(0, t.get(), 0);
    return true;
  }
  if (FAILED(hr)) return false;
  com_ptr<IMFSample> sample;
  if (own) sample = own;
  else sample.attach(ob.pSample);
  if (sample) emit(sample.get());
  return true;
}

void VideoEncoder::Impl::emit(IMFSample* s) {
  LONGLONG t = 0, d = 0;
  s->GetSampleTime(&t);
  if (FAILED(s->GetSampleDuration(&d)) || d <= 0) d = frameDuration;
  com_ptr<IMFMediaBuffer> buf;
  if (FAILED(s->ConvertToContiguousBuffer(buf.put()))) return;
  BYTE* p = nullptr;
  DWORD len = 0;
  if (FAILED(buf->Lock(&p, nullptr, &len))) return;
  UINT32 clean = 0;
  const bool key = (SUCCEEDED(s->GetUINT32(MFSampleExtension_CleanPoint, &clean)) && clean) || isKeyframe(cfg.codec, p, len);
  Packet pk{kVideoTrack, t, d, key, makeBytes(p, len)};
  buf->Unlock();
  sink(std::move(pk));
}

com_ptr<IMFSample> VideoEncoder::Impl::makeSample(ID3D11Texture2D* tex, Ticks pts) {
  com_ptr<IMFMediaBuffer> buf;
  if (d3dAware) {
    check_hresult(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), tex, 0, FALSE, buf.put()));
    DWORD len = 0;
    buf.as<IMF2DBuffer>()->GetContiguousLength(&len);
    buf->SetCurrentLength(len);
  } else {
    gpu.context->CopyResource(staging.get(), tex);
    D3D11_MAPPED_SUBRESOURCE m{};
    check_hresult(gpu.context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &m));
    const DWORD ySize = DWORD(cfg.width * cfg.height);
    const DWORD total = ySize * 3 / 2;
    check_hresult(MFCreateMemoryBuffer(total, buf.put()));
    BYTE* dst = nullptr;
    buf->Lock(&dst, nullptr, nullptr);
    const auto* src = static_cast<const BYTE*>(m.pData);
    for (int y = 0; y < cfg.height; ++y) memcpy(dst + size_t(y) * cfg.width, src + size_t(y) * m.RowPitch, size_t(cfg.width));
    const BYTE* uv = src + size_t(m.RowPitch) * cfg.height;
    for (int y = 0; y < cfg.height / 2; ++y)
      memcpy(dst + ySize + size_t(y) * cfg.width, uv + size_t(y) * m.RowPitch, size_t(cfg.width));
    buf->Unlock();
    buf->SetCurrentLength(total);
    gpu.context->Unmap(staging.get(), 0);
  }
  com_ptr<IMFSample> s;
  check_hresult(MFCreateSample(s.put()));
  s->AddBuffer(buf.get());
  s->SetSampleTime(pts);
  s->SetSampleDuration(frameDuration);
  return s;
}

void VideoEncoder::Impl::shutdown() {
  if (mft && streaming) mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
  if (events)
    if (auto sd = mft.try_as<IMFShutdown>()) sd->Shutdown();
  if (eventThread.joinable()) eventThread.join();
}

VideoEncoder::VideoEncoder(const GpuDevice& gpu, const VideoEncoderConfig& cfg, PacketSink sink) {
  MFT_REGISTER_TYPE_INFO outInfo{MFMediaType_Video, subtypeFor(cfg.codec)};
  const UINT32 flags =
      (cfg.forceSoftware ? MFT_ENUM_FLAG_SYNCMFT : MFT_ENUM_FLAG_HARDWARE) | MFT_ENUM_FLAG_SORTANDFILTER;
  IMFActivate** acts = nullptr;
  UINT32 count = 0;
  check_hresult(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, flags, nullptr, &outInfo, &acts, &count));
  std::vector<com_ptr<IMFActivate>> list(count);
  for (UINT32 i = 0; i < count; ++i) list[i].attach(acts[i]);
  CoTaskMemFree(acts);
  std::string lastError = "nessun encoder " + codecName(cfg.codec);
  for (auto& a : list) {
    auto impl = std::make_unique<Impl>();
    impl->cfg = cfg;
    impl->sink = sink;
    impl->gpu = gpu;
    impl->hardware = !cfg.forceSoftware;
    impl->frameDuration = kTicksPerSecond / cfg.fps;
    try {
      impl->init(a.get());
      impl_ = std::move(impl);
      return;
    } catch (const winrt::hresult_error& e) {
      lastError = std::format("{}: {}", impl->name, wideToUtf8(e.message()));
      impl->shutdown();
    }
  }
  throw std::runtime_error(lastError);
}

VideoEncoder::~VideoEncoder() {
  if (impl_) impl_->shutdown();
}

void VideoEncoder::encode(ID3D11Texture2D* nv12, Ticks pts) {
  auto& d = *impl_;
  if (d.async) {
    std::unique_lock lk(d.mu);
    if (!d.cv.wait_for(lk, std::chrono::milliseconds(50), [&] { return d.needInput > 0 || d.failed; }) || d.failed)
      return;
    --d.needInput;
  }
  auto sample = d.makeSample(nv12, pts);
  if (d.forceKey.exchange(false) && d.codecApi) setProp(d.codecApi.get(), CODECAPI_AVEncVideoForceKeyFrame, 1);
  HRESULT hr = d.mft->ProcessInput(0, sample.get(), 0);
  if (!d.async) {
    if (hr == MF_E_NOTACCEPTING) {
      while (d.pullOutput()) {
      }
      d.mft->ProcessInput(0, sample.get(), 0);
    }
    while (d.pullOutput()) {
    }
  }
}

void VideoEncoder::requestKeyframe() { impl_->forceKey = true; }

void VideoEncoder::flush() {
  auto& d = *impl_;
  d.mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
  if (d.async) {
    {
      std::lock_guard lk(d.mu);
      d.drained = false;
    }
    d.mft->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
    std::unique_lock lk(d.mu);
    d.cv.wait_for(lk, std::chrono::seconds(2), [&] { return d.drained || d.failed; });
  } else {
    d.mft->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
    while (d.pullOutput()) {
    }
  }
}

bool VideoEncoder::hardware() const { return impl_->hardware; }
std::string VideoEncoder::name() const { return impl_->name; }

TrackInfo VideoEncoder::trackInfo() const {
  TrackInfo t;
  t.id = kVideoTrack;
  t.kind = TrackKind::Video;
  t.codec = impl_->cfg.codec;
  t.name = "Video";
  t.width = impl_->cfg.width;
  t.height = impl_->cfg.height;
  t.fps = impl_->cfg.fps;
  return t;
}

std::unique_ptr<VideoEncoder> createVideoEncoderWithFallback(const GpuDevice& gpu, VideoEncoderConfig cfg,
                                                             VideoEncoder::PacketSink sink, std::string* warning) {
  std::vector<std::pair<Codec, bool>> order{{cfg.codec, cfg.forceSoftware}};
  if (!cfg.forceSoftware) {
    if (cfg.codec != Codec::HEVC) order.push_back({Codec::HEVC, false});
    if (cfg.codec != Codec::H264) order.push_back({Codec::H264, false});
  }
  order.push_back({Codec::H264, true});
  std::string firstError;
  for (size_t i = 0; i < order.size(); ++i) {
    VideoEncoderConfig c = cfg;
    c.codec = order[i].first;
    c.forceSoftware = order[i].second;
    try {
      auto e = std::make_unique<VideoEncoder>(gpu, c, sink);
      if (i > 0 && warning)
        *warning = trf("warn.encoder", codecName(cfg.codec), firstError, e->name());
      return e;
    } catch (const std::exception& ex) {
      if (firstError.empty()) firstError = ex.what();
    }
  }
  throw std::runtime_error("Nessun encoder video disponibile: " + firstError);
}

}  // namespace clipper
