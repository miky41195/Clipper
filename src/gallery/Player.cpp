#include "gallery/Player.h"
#include "core/Clock.h"
#include "core/Text.h"
#include "gallery/AudioOut.h"
#include <windows.h>
#include <combaseapi.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/hwcontext.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace clipper {
namespace fs = std::filesystem;

double clampSeek(double s, double duration) {
  if (duration <= 0) return 0;
  return std::clamp(s, 0.0, std::max(0.0, duration - 0.1));
}

void mixTracks(const std::vector<const float*>& tracks, const std::vector<float>& gains, float* out, size_t samples) {
  for (size_t i = 0; i < samples; ++i) {
    float v = 0;
    for (size_t k = 0; k < tracks.size(); ++k) v += tracks[k][i] * gains[k];
    out[i] = std::clamp(v, -1.f, 1.f);
  }
}

namespace {
constexpr int kRate = 48000;
constexpr double kGapTolerance = 0.002;
constexpr size_t kMaxVideoFrames = 6;

AVPixelFormat pickHwFormat(AVCodecContext* ctx, const AVPixelFormat* fmts) {
  for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p)
    if (*p == AV_PIX_FMT_D3D11) return *p;
  return avcodec_default_get_format(ctx, fmts);
}

double nowSeconds() { return double(now()) / kTicksPerSecond; }
}  // namespace

struct AudioTrackState {
  int stream = -1;
  AVCodecContext* dec = nullptr;
  SwrContext* swr = nullptr;
  std::string name;
  std::deque<float> ring;  // interleaved stereo, starts at the playback clock position
  double endTime = 0;      // timeline time at the end of `ring`
  std::atomic<float> gain{1.f};
  std::atomic<bool> enabled{true};
  std::atomic<float> level{0.f};
};

struct Player::Impl {
  std::string error;
  AVFormatContext* fmt = nullptr;
  int vstream = -1;
  AVCodecContext* vdec = nullptr;
  AVBufferRef* hw = nullptr;
  SwsContext* sws = nullptr;
  int swsW = 0, swsH = 0, swsFmt = -1;
  int outW = 0, outH = 0;
  double duration = 0;
  std::vector<std::unique_ptr<AudioTrackState>> tracks;

  std::mutex mu;
  std::condition_variable cv;
  struct VFrame {
    double pts;
    std::vector<uint8_t> bgra;
  };
  std::deque<VFrame> video;
  bool shownAny = false;
  double lastShown = -1;
  double clockBase = 0;
  int64_t consumed = 0;
  double wallStart = 0;
  bool playing = false;
  bool eof = false;
  double demuxTime = 0;
  double dropBefore = 0;
  double seekRequest = -1;
  std::atomic<bool> stop{false};
  std::thread worker;
  std::unique_ptr<AudioOut> out;
  std::vector<std::vector<float>> scratch;
  std::vector<float> mixBuf;

  bool audioClock() const { return out && !tracks.empty(); }

  double positionLocked() const {
    if (audioClock()) return std::min(duration, clockBase + double(consumed) / kRate);
    return std::min(duration, clockBase + (playing ? nowSeconds() - wallStart : 0));
  }

  bool fullLocked() const {
    const bool videoFull = vstream < 0 || video.size() >= kMaxVideoFrames;
    if (!audioClock()) return videoFull;
    size_t minRing = SIZE_MAX, maxRing = 0;
    for (const auto& t : tracks) {
      minRing = std::min(minRing, t->ring.size());
      maxRing = std::max(maxRing, t->ring.size());
    }
    return maxRing >= size_t(3 * kRate * 2) || (videoFull && minRing >= size_t(kRate));  // 0.5 s stereo
  }

  void padTo(AudioTrackState& t, double time) {
    if (time - t.endTime <= kGapTolerance) return;
    const size_t frames = size_t((time - t.endTime) * kRate);
    t.ring.insert(t.ring.end(), frames * 2, 0.f);
    t.endTime += double(frames) / kRate;
  }

  void pushAudio(AudioTrackState& t, double pts, const float* data, size_t frames) {
    size_t skip = 0;
    if (pts < dropBefore) skip = size_t((dropBefore - pts) * kRate);
    const double gap = pts - t.endTime;
    if (gap > kGapTolerance) padTo(t, pts);
    else if (gap < -kGapTolerance) skip = std::max(skip, size_t(-gap * kRate));
    if (skip >= frames) return;
    t.ring.insert(t.ring.end(), data + skip * 2, data + frames * 2);
    t.endTime = std::max(t.endTime, pts + double(frames) / kRate);
  }

  void render(float* o, size_t frames) {
    std::lock_guard lk(mu);
    std::fill(o, o + frames * 2, 0.f);
    if (!playing) return;
    size_t avail = SIZE_MAX, most = 0;
    for (const auto& t : tracks) {
      avail = std::min(avail, t->ring.size() / 2);
      most = std::max(most, t->ring.size() / 2);
    }
    if (eof) avail = most;
    const size_t n = std::min(frames, avail);
    if (n == 0) {
      if (eof) playing = false;  // reached the end
      return;
    }
    scratch.resize(tracks.size());
    std::vector<const float*> ptrs;
    std::vector<float> gains;
    for (size_t k = 0; k < tracks.size(); ++k) {
      auto& t = *tracks[k];
      auto& s = scratch[k];
      s.assign(n * 2, 0.f);
      const size_t take = std::min(n * 2, t.ring.size());
      std::copy_n(t.ring.begin(), take, s.begin());
      t.ring.erase(t.ring.begin(), t.ring.begin() + std::ptrdiff_t(take));
      float peak = 0;
      for (size_t i = 0; i < take; ++i) peak = std::max(peak, std::fabs(s[i]));
      t.level = peak;
      ptrs.push_back(s.data());
      gains.push_back(t.enabled ? t.gain.load() : 0.f);
    }
    mixTracks(ptrs, gains, o, n * 2);
    consumed += int64_t(n);
    cv.notify_all();
  }

  void toBgra(AVFrame* frame, double pts) {
    if (!sws || frame->width != swsW || frame->height != swsH || frame->format != swsFmt) {
      sws_freeContext(sws);
      sws = sws_getContext(frame->width, frame->height, AVPixelFormat(frame->format), outW, outH, AV_PIX_FMT_BGRA,
                           SWS_BILINEAR, nullptr, nullptr, nullptr);
      swsW = frame->width;
      swsH = frame->height;
      swsFmt = frame->format;
      if (!sws) return;
    }
    VFrame vf{pts, std::vector<uint8_t>(size_t(outW) * outH * 4)};
    uint8_t* dst[1] = {vf.bgra.data()};
    const int stride[1] = {outW * 4};
    sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, stride);
    std::lock_guard lk(mu);
    video.push_back(std::move(vf));
  }

  void decodeVideo(AVPacket* pkt, AVFrame* fr, AVFrame* sw) {
    if (avcodec_send_packet(vdec, pkt) < 0) return;
    const double tb = av_q2d(fmt->streams[vstream]->time_base);
    while (avcodec_receive_frame(vdec, fr) >= 0) {
      const double pts = double(fr->best_effort_timestamp) * tb;
      if (pts + 0.001 >= dropBefore) {
        if (fr->format == AV_PIX_FMT_D3D11) {
          av_frame_unref(sw);
          if (av_hwframe_transfer_data(sw, fr, 0) >= 0) toBgra(sw, pts);
        } else {
          toBgra(fr, pts);
        }
      }
      av_frame_unref(fr);
    }
  }

  void decodeAudio(AudioTrackState& t, AVPacket* pkt, AVFrame* fr) {
    if (avcodec_send_packet(t.dec, pkt) < 0) return;
    const double tb = av_q2d(fmt->streams[t.stream]->time_base);
    std::vector<float> buf;
    while (avcodec_receive_frame(t.dec, fr) >= 0) {
      if (!t.swr) {
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        swr_alloc_set_opts2(&t.swr, &stereo, AV_SAMPLE_FMT_FLT, kRate, &fr->ch_layout, AVSampleFormat(fr->format),
                            fr->sample_rate, 0, nullptr);
        if (!t.swr || swr_init(t.swr) < 0) return;
      }
      const int maxOut = swr_get_out_samples(t.swr, fr->nb_samples);
      buf.resize(size_t(std::max(maxOut, 0)) * 2);
      uint8_t* outp[1] = {reinterpret_cast<uint8_t*>(buf.data())};
      const int got = swr_convert(t.swr, outp, maxOut, const_cast<const uint8_t**>(fr->extended_data), fr->nb_samples);
      const double pts = double(fr->best_effort_timestamp) * tb;
      if (got > 0) {
        std::lock_guard lk(mu);
        pushAudio(t, pts, buf.data(), size_t(got));
      }
      av_frame_unref(fr);
    }
  }

  void doSeek(double target) {
    av_seek_frame(fmt, -1, int64_t(target * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD);
    if (vdec) avcodec_flush_buffers(vdec);
    for (auto& t : tracks) {
      avcodec_flush_buffers(t->dec);
      swr_free(&t->swr);
    }
    std::lock_guard lk(mu);
    video.clear();
    for (auto& t : tracks) {
      t->ring.clear();
      t->endTime = target;
    }
    dropBefore = target;
    clockBase = target;
    consumed = 0;
    wallStart = nowSeconds();
    eof = false;
    demuxTime = target;
    shownAny = false;
    lastShown = -1;
  }

  void run() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    AVPacket* pkt = av_packet_alloc();
    AVFrame* fr = av_frame_alloc();
    AVFrame* sw = av_frame_alloc();
    while (!stop) {
      double target = -1;
      {
        std::unique_lock lk(mu);
        target = std::exchange(seekRequest, -1.0);
        if (target < 0 && (eof || fullLocked())) {
          cv.wait_for(lk, std::chrono::milliseconds(20));
          continue;
        }
      }
      if (target >= 0) {
        doSeek(target);
        continue;
      }
      if (av_read_frame(fmt, pkt) < 0) {
        if (vdec) decodeVideo(nullptr, fr, sw);
        for (auto& t : tracks) decodeAudio(*t, nullptr, fr);
        std::lock_guard lk(mu);
        for (auto& t : tracks) padTo(*t, duration);
        eof = true;
        continue;
      }
      const double tb = av_q2d(fmt->streams[pkt->stream_index]->time_base);
      const double pktTime = pkt->pts == AV_NOPTS_VALUE ? -1 : double(pkt->pts) * tb;
      if (pkt->stream_index == vstream) {
        decodeVideo(pkt, fr, sw);
      } else if (audioClock()) {
        for (auto& t : tracks)
          if (t->stream == pkt->stream_index) decodeAudio(*t, pkt, fr);
      }
      av_packet_unref(pkt);
      if (pktTime >= 0) {
        std::lock_guard lk(mu);
        demuxTime = std::max(demuxTime, pktTime);
        // Tracks with no packets around now are silent there (app not playing): keep them aligned.
        for (auto& t : tracks) padTo(*t, demuxTime - 0.5);
      }
    }
    av_frame_free(&sw);
    av_frame_free(&fr);
    av_packet_free(&pkt);
    CoUninitialize();
  }
};

Player::Player(const fs::path& file) : d_(std::make_unique<Impl>()) {
  auto& d = *d_;
  if (avformat_open_input(&d.fmt, pathToUtf8(file).c_str(), nullptr, nullptr) < 0 || avformat_find_stream_info(d.fmt, nullptr) < 0) {
    d.error = "file non leggibile";
    return;
  }
  d.duration = d.fmt->duration > 0 ? double(d.fmt->duration) / AV_TIME_BASE : 0;
  const AVCodec* vcodec = nullptr;
  d.vstream = av_find_best_stream(d.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &vcodec, 0);
  if (d.vstream >= 0 && vcodec) {
    d.vdec = avcodec_alloc_context3(vcodec);
    avcodec_parameters_to_context(d.vdec, d.fmt->streams[d.vstream]->codecpar);
    if (av_hwdevice_ctx_create(&d.hw, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0) == 0) {
      d.vdec->hw_device_ctx = av_buffer_ref(d.hw);
      d.vdec->get_format = pickHwFormat;
    } else {
      d.vdec->thread_count = 0;
    }
    if (avcodec_open2(d.vdec, vcodec, nullptr) < 0) {
      d.error = "video non decodificabile";
      return;
    }
    const AVCodecParameters* par = d.fmt->streams[d.vstream]->codecpar;
    d.outW = std::min(par->width, 1280) & ~1;
    d.outH = std::max(2, int(std::lround(double(par->height) * d.outW / std::max(1, par->width))) & ~1);
  }
  for (unsigned i = 0; i < d.fmt->nb_streams; ++i) {
    AVStream* st = d.fmt->streams[i];
    if (st->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) continue;
    const AVCodec* c = avcodec_find_decoder(st->codecpar->codec_id);
    if (!c) continue;
    auto t = std::make_unique<AudioTrackState>();
    t->stream = int(i);
    t->dec = avcodec_alloc_context3(c);
    avcodec_parameters_to_context(t->dec, st->codecpar);
    if (avcodec_open2(t->dec, c, nullptr) < 0) {
      avcodec_free_context(&t->dec);
      continue;
    }
    const AVDictionaryEntry* title = av_dict_get(st->metadata, "title", nullptr, 0);
    if (!title || !title->value[0]) title = av_dict_get(st->metadata, "handler_name", nullptr, 0);
    t->name = title && title->value[0] && std::string_view(title->value) != "SoundHandler" ? title->value
                                                                                            : "Audio " + std::to_string(d.tracks.size() + 1);
    d.tracks.push_back(std::move(t));
  }
  if (!d.tracks.empty()) {
    try {
      d.out = std::make_unique<AudioOut>([this](float* o, size_t n) { d_->render(o, n); });
    } catch (...) {
      d.out.reset();  // no output device: play video on the wall clock
    }
  }
  d.worker = std::thread([this] { d_->run(); });
}

Player::~Player() {
  if (!d_) return;
  d_->out.reset();
  d_->stop = true;
  d_->cv.notify_all();
  if (d_->worker.joinable()) d_->worker.join();
  sws_freeContext(d_->sws);
  avcodec_free_context(&d_->vdec);
  for (auto& t : d_->tracks) {
    swr_free(&t->swr);
    avcodec_free_context(&t->dec);
  }
  av_buffer_unref(&d_->hw);
  avformat_close_input(&d_->fmt);
}

bool Player::ok() const { return d_->error.empty(); }
const std::string& Player::error() const { return d_->error; }
double Player::duration() const { return d_->duration; }

std::vector<std::string> Player::trackNames() const {
  std::vector<std::string> out;
  for (const auto& t : d_->tracks) out.push_back(t->name);
  return out;
}

void Player::setTrackGain(int i, float g) {
  if (i >= 0 && i < int(d_->tracks.size())) d_->tracks[size_t(i)]->gain = std::clamp(g, 0.f, 2.f);
}
void Player::setTrackEnabled(int i, bool on) {
  if (i >= 0 && i < int(d_->tracks.size())) d_->tracks[size_t(i)]->enabled = on;
}
float Player::trackGain(int i) const { return i >= 0 && i < int(d_->tracks.size()) ? d_->tracks[size_t(i)]->gain.load() : 0.f; }
bool Player::trackEnabled(int i) const { return i >= 0 && i < int(d_->tracks.size()) && d_->tracks[size_t(i)]->enabled; }
float Player::trackLevel(int i) const { return i >= 0 && i < int(d_->tracks.size()) ? d_->tracks[size_t(i)]->level.load() : 0.f; }

void Player::play() {
  if (!ok()) return;
  bool restart = false;
  {
    std::lock_guard lk(d_->mu);
    restart = d_->positionLocked() >= d_->duration - 0.15;
  }
  if (restart) seek(0);
  std::lock_guard lk(d_->mu);
  d_->wallStart = nowSeconds();
  d_->playing = true;
}

void Player::pause() {
  std::lock_guard lk(d_->mu);
  if (!d_->audioClock()) d_->clockBase = d_->positionLocked();
  d_->playing = false;
}

bool Player::playing() const {
  std::lock_guard lk(d_->mu);
  return d_->playing;
}

void Player::seek(double s) {
  std::lock_guard lk(d_->mu);
  d_->seekRequest = clampSeek(s, d_->duration);
  d_->cv.notify_all();
}

double Player::position() const {
  std::lock_guard lk(d_->mu);
  return d_->seekRequest >= 0 ? d_->seekRequest : d_->positionLocked();
}

bool Player::takeFrame(std::vector<uint8_t>& bgra, int& w, int& h) {
  std::lock_guard lk(d_->mu);
  auto& v = d_->video;
  if (v.empty()) return false;
  const double pos = d_->positionLocked();
  if (d_->playing && !d_->audioClock() && d_->eof && v.size() == 1 && pos >= d_->duration - 0.05) d_->playing = false;
  while (v.size() >= 2 && v[1].pts <= pos + 0.008) v.pop_front();
  const auto& f = v.front();
  if (d_->shownAny && f.pts == d_->lastShown) return false;
  if (d_->shownAny && d_->playing && f.pts > pos + 0.02) return false;
  bgra = f.bgra;
  w = d_->outW;
  h = d_->outH;
  d_->lastShown = f.pts;
  d_->shownAny = true;
  d_->cv.notify_all();
  return true;
}

}  // namespace clipper
