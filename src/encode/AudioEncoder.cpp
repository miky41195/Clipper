#include "encode/AudioEncoder.h"
#include <mfapi.h>
#include <mferror.h>
#include <wmcodecdsp.h>
#include <cstring>

namespace clipper {
using winrt::check_hresult;
using winrt::com_ptr;

AudioEncoder::AudioEncoder(const AudioEncoderConfig& cfg, int trackId, PacketSink sink)
    : cfg_(cfg), track_(trackId), sink_(std::move(sink)) {
  check_hresult(CoCreateInstance(CLSID_AACMFTEncoder, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(mft_.put())));

  com_ptr<IMFMediaType> in;
  check_hresult(MFCreateMediaType(in.put()));
  in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
  in->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
  in->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
  in->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, UINT32(cfg.sampleRate));
  in->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, UINT32(cfg.channels));
  in->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, UINT32(cfg.channels * 2));
  in->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, UINT32(cfg.sampleRate * cfg.channels * 2));

  com_ptr<IMFMediaType> out;
  check_hresult(MFCreateMediaType(out.put()));
  out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
  out->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
  out->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
  out->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, UINT32(cfg.sampleRate));
  out->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, UINT32(cfg.channels));
  out->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, UINT32(cfg.kbps * 125));
  out->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);
  out->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);

  // The MS AAC encoder wants the input type first; try the other order if it refuses.
  if (FAILED(mft_->SetInputType(0, in.get(), 0)) || FAILED(mft_->SetOutputType(0, out.get(), 0))) {
    check_hresult(mft_->SetOutputType(0, out.get(), 0));
    check_hresult(mft_->SetInputType(0, in.get(), 0));
  }

  com_ptr<IMFMediaType> cur;
  check_hresult(mft_->GetOutputCurrentType(0, cur.put()));
  UINT32 blobSize = 0;
  if (SUCCEEDED(cur->GetBlobSize(MF_MT_USER_DATA, &blobSize)) && blobSize > 12) {
    std::vector<uint8_t> blob(blobSize);
    cur->GetBlob(MF_MT_USER_DATA, blob.data(), blobSize, nullptr);
    extradata_.assign(blob.begin() + 12, blob.end());  // skip HEAACWAVEINFO tail
  }
  check_hresult(mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0));
  check_hresult(mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));
}

AudioEncoder::~AudioEncoder() {
  if (mft_) mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
}

void AudioEncoder::encode(const int16_t* pcm, size_t frames, Ticks pts) {
  const DWORD bytes = DWORD(frames * size_t(cfg_.channels) * sizeof(int16_t));
  com_ptr<IMFMediaBuffer> buf;
  check_hresult(MFCreateMemoryBuffer(bytes, buf.put()));
  BYTE* p = nullptr;
  buf->Lock(&p, nullptr, nullptr);
  std::memcpy(p, pcm, bytes);
  buf->Unlock();
  buf->SetCurrentLength(bytes);
  com_ptr<IMFSample> s;
  check_hresult(MFCreateSample(s.put()));
  s->AddBuffer(buf.get());
  s->SetSampleTime(pts);
  s->SetSampleDuration(Ticks(frames) * kTicksPerSecond / cfg_.sampleRate);
  HRESULT hr = mft_->ProcessInput(0, s.get(), 0);
  if (hr == MF_E_NOTACCEPTING) {
    while (pullOne()) {
    }
    mft_->ProcessInput(0, s.get(), 0);
  }
  while (pullOne()) {
  }
}

void AudioEncoder::flush() {
  mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
  mft_->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
  while (pullOne()) {
  }
}

bool AudioEncoder::pullOne() {
  MFT_OUTPUT_STREAM_INFO info{};
  if (FAILED(mft_->GetOutputStreamInfo(0, &info))) return false;
  com_ptr<IMFSample> s;
  com_ptr<IMFMediaBuffer> buf;
  if (FAILED(MFCreateSample(s.put())) || FAILED(MFCreateMemoryBuffer(info.cbSize ? info.cbSize : 8192, buf.put())))
    return false;
  s->AddBuffer(buf.get());
  MFT_OUTPUT_DATA_BUFFER ob{};
  ob.pSample = s.get();
  DWORD status = 0;
  const HRESULT hr = mft_->ProcessOutput(0, 1, &ob, &status);
  if (ob.pEvents) ob.pEvents->Release();
  if (FAILED(hr)) return false;
  LONGLONG t = 0, d = 0;
  s->GetSampleTime(&t);
  s->GetSampleDuration(&d);
  com_ptr<IMFMediaBuffer> cb;
  if (FAILED(s->ConvertToContiguousBuffer(cb.put()))) return true;
  BYTE* p = nullptr;
  DWORD len = 0;
  cb->Lock(&p, nullptr, &len);
  if (len > 0) sink_(Packet{track_, t - kEncoderDelay, d, true, makeBytes(p, len)});
  cb->Unlock();
  return true;
}

}  // namespace clipper
