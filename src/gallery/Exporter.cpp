#include "gallery/Exporter.h"
#include "app/ClipNaming.h"
#include "core/Text.h"
#include "encode/AudioEncoder.h"
#include "output/ClipWriter.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <memory>
#include <stdexcept>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

namespace clipper {
namespace fs = std::filesystem;

namespace {
constexpr int kRate = 48000;
constexpr size_t kChunk = 1024;
constexpr AVRational kTicks{1, 10'000'000};

std::optional<Codec> codecFromAv(AVCodecID id) {
  switch (id) {
    case AV_CODEC_ID_H264: return Codec::H264;
    case AV_CODEC_ID_HEVC: return Codec::HEVC;
    case AV_CODEC_ID_AV1: return Codec::AV1;
    case AV_CODEC_ID_AAC: return Codec::AAC;
    default: return std::nullopt;
  }
}

std::string streamTitle(AVStream* st, size_t index) {
  for (const char* key : {"title", "handler_name"}) {
    const AVDictionaryEntry* e = av_dict_get(st->metadata, key, nullptr, 0);
    if (e && e->value[0] && std::string_view(e->value) != "SoundHandler") return e->value;
  }
  return "Audio " + std::to_string(index + 1);
}

struct MixInput {
  int stream = -1;
  size_t index = 0;
  float gain = 1.f;
  AVCodecContext* dec = nullptr;
  SwrContext* swr = nullptr;
  std::deque<float> ring;
  double endTime = 0;
  ~MixInput() {
    swr_free(&swr);
    avcodec_free_context(&dec);
  }
};

void padTo(MixInput& m, double t) {
  if (t - m.endTime <= 0.002) return;
  const size_t frames = size_t((t - m.endTime) * kRate);
  m.ring.insert(m.ring.end(), frames * 2, 0.f);
  m.endTime += double(frames) / kRate;
}

}  // namespace

fs::path exportClip(const fs::path& src, const ExportOptions& opt, const std::function<void(float)>& progress) {
  AVFormatContext* fmt = nullptr;
  if (avformat_open_input(&fmt, pathToUtf8(src).c_str(), nullptr, nullptr) < 0) throw std::runtime_error("clip non leggibile");
  std::unique_ptr<AVFormatContext, void (*)(AVFormatContext*)> guard(fmt, [](AVFormatContext* c) { avformat_close_input(&c); });
  if (avformat_find_stream_info(fmt, nullptr) < 0) throw std::runtime_error("clip non leggibile");
  const double duration = fmt->duration > 0 ? double(fmt->duration) / AV_TIME_BASE : 0;

  const int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (vs < 0) throw std::runtime_error("la clip non ha video");
  const AVCodecParameters* vpar = fmt->streams[vs]->codecpar;
  const auto vcodec = codecFromAv(vpar->codec_id);
  if (!vcodec) throw std::runtime_error("codec video non supportato per l'esportazione");
  TrackInfo video;
  video.id = kVideoTrack;
  video.kind = TrackKind::Video;
  video.codec = *vcodec;
  video.name = "Video";
  video.width = vpar->width;
  video.height = vpar->height;
  const AVRational fr = fmt->streams[vs]->avg_frame_rate;
  video.fps = fr.den ? int(std::lround(av_q2d(fr))) : 60;
  video.extradata.assign(vpar->extradata, vpar->extradata + vpar->extradata_size);
  std::vector<TrackInfo> tracks{video};

  // Audio inputs
  std::vector<std::unique_ptr<MixInput>> mix;
  std::vector<std::pair<int, int>> copies;  // source stream -> output track id
  size_t audioIndex = 0;
  for (unsigned i = 0; i < fmt->nb_streams; ++i) {
    AVStream* st = fmt->streams[i];
    if (st->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) continue;
    const size_t k = audioIndex++;
    if (k >= opt.keep.size() || !opt.keep[k]) continue;
    const AVCodec* c = avcodec_find_decoder(st->codecpar->codec_id);
    if (!c) continue;
    auto m = std::make_unique<MixInput>();
    m->stream = int(i);
    m->index = k;
    m->gain = k < opt.gain.size() ? std::clamp(opt.gain[k], 0.f, 2.f) : 1.f;
    m->dec = avcodec_alloc_context3(c);
    avcodec_parameters_to_context(m->dec, st->codecpar);
    if (avcodec_open2(m->dec, c, nullptr) < 0) continue;
    mix.push_back(std::move(m));
    if (opt.alsoSeparate && codecFromAv(st->codecpar->codec_id) == Codec::AAC) {
      TrackInfo t;
      t.id = 2 + int(k);
      t.kind = TrackKind::Audio;
      t.codec = Codec::AAC;
      t.name = streamTitle(st, k);
      t.sampleRate = st->codecpar->sample_rate;
      t.channels = st->codecpar->ch_layout.nb_channels;
      t.extradata.assign(st->codecpar->extradata, st->codecpar->extradata + st->codecpar->extradata_size);
      copies.emplace_back(int(i), t.id);
      tracks.push_back(t);
    }
  }

  const std::string ext = pathToUtf8(src.extension());
  const fs::path out = uniquePath(src.parent_path(), pathToUtf8(src.stem()) + "_export", ext);
  std::unique_ptr<ClipWriter> writer;
  std::unique_ptr<AudioEncoder> encoder;
  if (!mix.empty()) {
    encoder = std::make_unique<AudioEncoder>(AudioEncoderConfig{kRate, 2, 192}, 1, [&](Packet p) { writer->write(p); });
    TrackInfo m;
    m.id = 1;
    m.kind = TrackKind::Audio;
    m.codec = Codec::AAC;
    m.name = "Mix";
    m.extradata = encoder->extradata();
    tracks.insert(tracks.begin() + 1, m);
  }
  writer = std::make_unique<ClipWriter>(out, toLowerAscii(ext) == ".mkv" ? "mkv" : "mp4", tracks);
  if (const AVDictionaryEntry* c = av_dict_get(fmt->metadata, "comment", nullptr, 0);
      c && std::string_view(c->value).starts_with(kAppTagPrefix))
    writer->setApp(std::string(std::string_view(c->value).substr(std::string_view(kAppTagPrefix).size())));

  int64_t mixFrames = 0;  // frames already encoded
  std::vector<float> chunkF(kChunk * 2);
  std::vector<int16_t> chunkS(kChunk * 2);
  auto mixAvailable = [&](bool final) {
    while (!mix.empty()) {
      size_t avail = SIZE_MAX, most = 0;
      for (auto& m : mix) {
        avail = std::min(avail, m->ring.size() / 2);
        most = std::max(most, m->ring.size() / 2);
      }
      if (final) avail = most;
      const size_t n = std::min(avail, kChunk);
      if (n == 0 || (!final && n < kChunk)) return;
      std::fill(chunkF.begin(), chunkF.end(), 0.f);
      for (auto& m : mix) {
        const size_t take = std::min(n * 2, m->ring.size());
        for (size_t i = 0; i < take; ++i) chunkF[i] += m->ring[i] * m->gain;
        m->ring.erase(m->ring.begin(), m->ring.begin() + std::ptrdiff_t(take));
      }
      for (size_t i = 0; i < n * 2; ++i) chunkS[i] = int16_t(std::lround(std::clamp(chunkF[i], -1.f, 1.f) * 32767.f));
      encoder->encode(chunkS.data(), n, Ticks(mixFrames) * kTicksPerSecond / kRate);
      mixFrames += int64_t(n);
    }
  };

  AVPacket* pkt = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  std::vector<float> buf;
  auto decode = [&](MixInput& m, AVPacket* p) {
    if (avcodec_send_packet(m.dec, p) < 0) return;
    const double tb = av_q2d(fmt->streams[m.stream]->time_base);
    while (avcodec_receive_frame(m.dec, frame) >= 0) {
      if (!m.swr) {
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        swr_alloc_set_opts2(&m.swr, &stereo, AV_SAMPLE_FMT_FLT, kRate, &frame->ch_layout, AVSampleFormat(frame->format),
                            frame->sample_rate, 0, nullptr);
        if (!m.swr || swr_init(m.swr) < 0) return;
      }
      const int maxOut = swr_get_out_samples(m.swr, frame->nb_samples);
      buf.resize(size_t(std::max(0, maxOut)) * 2);
      uint8_t* outp[1] = {reinterpret_cast<uint8_t*>(buf.data())};
      const int got = swr_convert(m.swr, outp, maxOut, const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
      const double pts = double(frame->best_effort_timestamp) * tb;
      if (got > 0) {
        size_t skip = 0;
        const double gap = pts - m.endTime;
        if (gap > 0.002) padTo(m, pts);
        else if (gap < -0.002) skip = size_t(-gap * kRate);
        if (skip < size_t(got)) {
          m.ring.insert(m.ring.end(), buf.begin() + std::ptrdiff_t(skip * 2), buf.begin() + std::ptrdiff_t(got) * 2);
          m.endTime = std::max(m.endTime, pts + double(got) / kRate);
        }
      }
      av_frame_unref(frame);
    }
  };

  double demuxTime = 0;
  while (av_read_frame(fmt, pkt) >= 0) {
    AVStream* st = fmt->streams[pkt->stream_index];
    const double t = pkt->pts == AV_NOPTS_VALUE ? demuxTime : double(pkt->pts) * av_q2d(st->time_base);
    if (pkt->stream_index == vs) {
      Packet p;
      p.track = kVideoTrack;
      p.pts = av_rescale_q(pkt->pts == AV_NOPTS_VALUE ? pkt->dts : pkt->pts, st->time_base, kTicks);
      p.duration = av_rescale_q(pkt->duration, st->time_base, kTicks);
      p.keyframe = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
      p.data = makeBytes(pkt->data, size_t(pkt->size));
      writer->write(p);
    } else {
      for (const auto& [stream, id] : copies)
        if (stream == pkt->stream_index) {
          writer->write(Packet{id, av_rescale_q(pkt->pts, st->time_base, kTicks), av_rescale_q(pkt->duration, st->time_base, kTicks),
                               true, makeBytes(pkt->data, size_t(pkt->size))});
        }
      for (auto& m : mix)
        if (m->stream == pkt->stream_index) decode(*m, pkt);
    }
    av_packet_unref(pkt);
    demuxTime = std::max(demuxTime, t);
    for (auto& m : mix) padTo(*m, demuxTime - 0.5);
    mixAvailable(false);
    if (progress && duration > 0) progress(float(std::min(0.99, demuxTime / duration)));
  }
  for (auto& m : mix) {
    decode(*m, nullptr);
    padTo(*m, duration);
  }
  if (!mix.empty()) {
    mixAvailable(true);
    encoder->flush();
  }
  av_frame_free(&frame);
  av_packet_free(&pkt);
  writer->finish();
  if (progress) progress(1.f);
  return out;
}

}  // namespace clipper
