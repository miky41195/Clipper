#include "output/ClipWriter.h"
#include "core/Text.h"
#include <algorithm>
#include <cstring>
#include <format>
#include <stdexcept>
#include <unordered_map>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
}

namespace clipper {
namespace fs = std::filesystem;

namespace {

constexpr AVRational kTimeBase{1, 10'000'000};

AVCodecID toAv(Codec c) {
  switch (c) {
    case Codec::H264: return AV_CODEC_ID_H264;
    case Codec::HEVC: return AV_CODEC_ID_HEVC;
    case Codec::AV1: return AV_CODEC_ID_AV1;
    case Codec::AAC: return AV_CODEC_ID_AAC;
  }
  return AV_CODEC_ID_NONE;
}

std::string avErr(int e) {
  char b[AV_ERROR_MAX_STRING_SIZE]{};
  av_strerror(e, b, sizeof b);
  return b;
}

void check(int r, const char* what) {
  if (r < 0) throw std::runtime_error(std::format("{}: {}", what, avErr(r)));
}

const char* formatName(std::string_view container) { return container == "mkv" ? "matroska" : "mp4"; }

void setExtradata(AVCodecParameters* par, const std::vector<uint8_t>& d) {
  av_freep(&par->extradata);
  par->extradata_size = 0;
  if (d.empty()) return;
  par->extradata = static_cast<uint8_t*>(av_mallocz(d.size() + AV_INPUT_BUFFER_PADDING_SIZE));
  std::memcpy(par->extradata, d.data(), d.size());
  par->extradata_size = int(d.size());
}

}  // namespace

struct ClipWriter::Impl {
  fs::path finalPath;
  fs::path tmpPath;
  std::string container;
  std::vector<TrackInfo> tracks;
  AVFormatContext* fmt = nullptr;
  std::unordered_map<int, int> streamOf;
  std::vector<int64_t> lastDts;
  std::vector<Packet> pending;
  bool header = false;
  bool done = false;
  Ticks origin = 0;

  void writeHeader(const Packet& key);
  void writePacket(const Packet& p);
  void close(bool keep);
};

ClipWriter::ClipWriter(fs::path finalPath, std::string_view container, std::vector<TrackInfo> tracks)
    : impl_(std::make_unique<Impl>()) {
  auto& d = *impl_;
  d.finalPath = std::move(finalPath);
  d.tmpPath = d.finalPath;
  d.tmpPath += L".tmp";
  d.container = container;
  d.tracks = std::move(tracks);
  std::stable_partition(d.tracks.begin(), d.tracks.end(), [](const TrackInfo& t) { return t.kind == TrackKind::Video; });
  if (d.tracks.empty() || d.tracks.front().kind != TrackKind::Video)
    throw std::runtime_error("nessuna traccia video nella clip");
  if (d.finalPath.has_parent_path()) fs::create_directories(d.finalPath.parent_path());
  // The url must be set: +faststart reopens the file by name to move the moov atom.
  check(avformat_alloc_output_context2(&d.fmt, nullptr, formatName(container), pathToUtf8(d.tmpPath).c_str()),
        "alloc output");
  bool firstAudio = true;
  for (const auto& t : d.tracks) {
    AVStream* st = avformat_new_stream(d.fmt, nullptr);
    if (!st) throw std::runtime_error("new stream");
    st->time_base = kTimeBase;
    AVCodecParameters* par = st->codecpar;
    par->codec_id = toAv(t.codec);
    if (t.kind == TrackKind::Video) {
      par->codec_type = AVMEDIA_TYPE_VIDEO;
      par->width = t.width;
      par->height = t.height;
      par->color_primaries = AVCOL_PRI_BT709;
      par->color_trc = AVCOL_TRC_BT709;
      par->color_space = AVCOL_SPC_BT709;
      par->color_range = AVCOL_RANGE_MPEG;
      st->avg_frame_rate = AVRational{t.fps, 1};
      st->disposition = AV_DISPOSITION_DEFAULT;
    } else {
      par->codec_type = AVMEDIA_TYPE_AUDIO;
      par->sample_rate = t.sampleRate;
      av_channel_layout_default(&par->ch_layout, t.channels);
      par->frame_size = 1024;
      if (firstAudio) {
        st->disposition = AV_DISPOSITION_DEFAULT;
        firstAudio = false;
      }
    }
    setExtradata(par, t.extradata);
    av_dict_set(&st->metadata, "title", t.name.c_str(), 0);
    av_dict_set(&st->metadata, "handler_name", t.name.c_str(), 0);
    d.streamOf[t.id] = st->index;
  }
  d.lastDts.assign(d.tracks.size(), INT64_MIN);
  check(avio_open(&d.fmt->pb, pathToUtf8(d.tmpPath).c_str(), AVIO_FLAG_WRITE), "open file");
}

ClipWriter::~ClipWriter() {
  if (impl_ && !impl_->done) {
    try {
      impl_->close(false);
    } catch (...) {
    }
  }
}

void ClipWriter::Impl::writeHeader(const Packet& key) {
  AVCodecParameters* par = fmt->streams[0]->codecpar;
  if (par->extradata_size == 0) setExtradata(par, extractExtradata(tracks[0].codec, *key.data));
  AVDictionary* opts = nullptr;
  if (container != "mkv") av_dict_set(&opts, "movflags", "+faststart", 0);
  const int r = avformat_write_header(fmt, &opts);
  av_dict_free(&opts);
  check(r, "write header");
  origin = key.pts;
  header = true;
}

void ClipWriter::Impl::writePacket(const Packet& p) {
  const int si = streamOf.at(p.track);
  AVStream* st = fmt->streams[si];
  AVPacket* pkt = av_packet_alloc();
  if (!pkt || av_new_packet(pkt, int(p.data->size())) < 0) {
    av_packet_free(&pkt);
    throw std::runtime_error("alloc packet");
  }
  std::memcpy(pkt->data, p.data->data(), p.data->size());
  pkt->stream_index = si;
  pkt->pts = pkt->dts = p.pts - origin;
  pkt->duration = p.duration;
  if (p.keyframe) pkt->flags |= AV_PKT_FLAG_KEY;
  av_packet_rescale_ts(pkt, kTimeBase, st->time_base);
  if (pkt->dts <= lastDts[si]) pkt->dts = lastDts[si] + 1;
  if (pkt->pts < pkt->dts) pkt->pts = pkt->dts;
  lastDts[si] = pkt->dts;
  const int r = av_interleaved_write_frame(fmt, pkt);
  av_packet_free(&pkt);
  check(r, "write packet");
}

void ClipWriter::Impl::close(bool keep) {
  if (fmt) {
    if (fmt->pb) avio_closep(&fmt->pb);
    avformat_free_context(fmt);
    fmt = nullptr;
  }
  done = true;
  std::error_code ec;
  if (keep) {
    fs::rename(tmpPath, finalPath, ec);
    if (ec) throw std::runtime_error("rinomina clip: " + ec.message());
  } else {
    fs::remove(tmpPath, ec);
  }
}

void ClipWriter::write(const Packet& p) {
  auto& d = *impl_;
  if (d.done || !p.data || !d.streamOf.contains(p.track)) return;
  if (!d.header) {
    if (p.track == kVideoTrack && p.keyframe) {
      d.writeHeader(p);
      d.writePacket(p);
      for (const auto& q : d.pending)
        if (q.pts >= d.origin) d.writePacket(q);
      d.pending.clear();
    } else if (p.track != kVideoTrack) {
      d.pending.push_back(p);
      if (d.pending.size() > 4096) d.pending.erase(d.pending.begin());
    }
    return;
  }
  if (p.pts < d.origin) return;
  d.writePacket(p);
}

void ClipWriter::finish() {
  auto& d = *impl_;
  if (d.done) return;
  if (!d.header) throw std::runtime_error("nessun keyframe video nella clip");
  check(av_write_trailer(d.fmt), "write trailer");
  d.close(true);
}

Ticks ClipWriter::origin() const { return impl_->origin; }

void ClipWriter::setApp(const std::string& app) {
  if (impl_->fmt && !impl_->header) av_dict_set(&impl_->fmt->metadata, "comment", (std::string(kAppTagPrefix) + app).c_str(), 0);
}

void writeSnapshot(const ReplayBuffer::Snapshot& snap, const fs::path& file, std::string_view container, const std::string& app) {
  if (snap.empty()) throw std::runtime_error("buffer vuoto");
  ClipWriter w(file, container, snap.tracks);
  if (!app.empty()) w.setApp(app);
  snap.forEach([&](const Packet& p) { w.write(p); });
  w.finish();
}

void remux(const fs::path& src, const fs::path& dst) {
  AVFormatContext* in = nullptr;
  check(avformat_open_input(&in, pathToUtf8(src).c_str(), nullptr, nullptr), "open input");
  std::unique_ptr<AVFormatContext, void (*)(AVFormatContext*)> inGuard(in, [](AVFormatContext* c) { avformat_close_input(&c); });
  check(avformat_find_stream_info(in, nullptr), "stream info");
  auto tmp = dst;
  tmp += L".tmp";
  const std::string ext = toLowerAscii(pathToUtf8(dst.extension()));
  const bool mkv = ext == ".mkv";
  AVFormatContext* out = nullptr;
  check(avformat_alloc_output_context2(&out, nullptr, mkv ? "matroska" : "mp4", pathToUtf8(tmp).c_str()),
        "alloc output");
  std::unique_ptr<AVFormatContext, void (*)(AVFormatContext*)> outGuard(out, [](AVFormatContext* c) {
    if (c->pb) avio_closep(&c->pb);
    avformat_free_context(c);
  });
  av_dict_copy(&out->metadata, in->metadata, 0);
  for (unsigned i = 0; i < in->nb_streams; ++i) {
    AVStream* is = in->streams[i];
    AVStream* os = avformat_new_stream(out, nullptr);
    if (!os) throw std::runtime_error("new stream");
    check(avcodec_parameters_copy(os->codecpar, is->codecpar), "copy params");
    os->codecpar->codec_tag = 0;
    os->time_base = is->time_base;
    os->disposition = is->disposition;
    av_dict_copy(&os->metadata, is->metadata, 0);
    if (const AVDictionaryEntry* t = av_dict_get(is->metadata, "title", nullptr, 0))
      av_dict_set(&os->metadata, "handler_name", t->value, 0);
  }
  check(avio_open(&out->pb, pathToUtf8(tmp).c_str(), AVIO_FLAG_WRITE), "open output");
  AVDictionary* opts = nullptr;
  if (!mkv) av_dict_set(&opts, "movflags", "+faststart", 0);
  int r = avformat_write_header(out, &opts);
  av_dict_free(&opts);
  check(r, "write header");
  AVPacket* pkt = av_packet_alloc();
  while (av_read_frame(in, pkt) >= 0) {
    av_packet_rescale_ts(pkt, in->streams[pkt->stream_index]->time_base, out->streams[pkt->stream_index]->time_base);
    pkt->pos = -1;
    r = av_interleaved_write_frame(out, pkt);
    if (r < 0) {
      av_packet_free(&pkt);
      check(r, "write packet");
    }
  }
  av_packet_free(&pkt);
  check(av_write_trailer(out), "write trailer");
  outGuard.reset();
  inGuard.reset();
  fs::rename(tmp, dst);
}

std::vector<uint8_t> extractExtradata(Codec codec, const std::vector<uint8_t>& keyframe) {
  const AVBitStreamFilter* f = av_bsf_get_by_name("extract_extradata");
  if (!f) return {};
  AVBSFContext* ctx = nullptr;
  if (av_bsf_alloc(f, &ctx) < 0) return {};
  ctx->par_in->codec_id = toAv(codec);
  ctx->par_in->codec_type = AVMEDIA_TYPE_VIDEO;
  std::vector<uint8_t> out;
  if (av_bsf_init(ctx) >= 0) {
    AVPacket* pkt = av_packet_alloc();
    if (av_new_packet(pkt, int(keyframe.size())) >= 0) {
      std::memcpy(pkt->data, keyframe.data(), keyframe.size());
      if (av_bsf_send_packet(ctx, pkt) >= 0 && av_bsf_receive_packet(ctx, pkt) >= 0) {
        size_t sz = 0;
        if (const uint8_t* sd = av_packet_get_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA, &sz)) out.assign(sd, sd + sz);
      }
    }
    av_packet_free(&pkt);
  }
  av_bsf_free(&ctx);
  return out;
}

}  // namespace clipper
