#include "gallery/Thumbnailer.h"
#include "core/Text.h"
#include "ui/ImageIO.h"
#include <windows.h>
#include <combaseapi.h>
#include <algorithm>
#include <format>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace clipper {
namespace fs = std::filesystem;

namespace {

fs::path cachePath(const fs::path& clip, const fs::path& cacheDir) {
  std::error_code ec;
  const auto size = fs::file_size(clip, ec);
  const auto mtime = fs::last_write_time(clip, ec).time_since_epoch().count();
  const size_t h = std::hash<std::wstring>{}(clip.wstring()) ^ (std::hash<uint64_t>{}(size) * 31) ^ (std::hash<int64_t>{}(mtime) * 131);
  return cacheDir / std::format("{:016x}.jpg", h);
}

// Decodes the first frame at or after `target` seconds. Returns false when the clip can't be decoded.
bool decodeFrameAt(const fs::path& clip, double target, std::vector<uint8_t>& bgra) {
  AVFormatContext* fmt = nullptr;
  if (avformat_open_input(&fmt, pathToUtf8(clip).c_str(), nullptr, nullptr) < 0) return false;
  bool ok = false;
  AVCodecContext* ctx = nullptr;
  AVPacket* pkt = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  SwsContext* sws = nullptr;
  do {
    if (avformat_find_stream_info(fmt, nullptr) < 0) break;
    const AVCodec* codec = nullptr;
    const int si = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (si < 0 || !codec) break;
    AVStream* st = fmt->streams[si];
    ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, st->codecpar);
    ctx->thread_count = 2;
    if (avcodec_open2(ctx, codec, nullptr) < 0) break;
    const double duration = fmt->duration > 0 ? double(fmt->duration) / AV_TIME_BASE : 0;
    target = std::min(target, duration * 0.5);
    const double tb = av_q2d(st->time_base);
    if (target > 0) av_seek_frame(fmt, si, int64_t(target / tb), AVSEEK_FLAG_BACKWARD);
    bool got = false;
    while (!got && av_read_frame(fmt, pkt) >= 0) {
      if (pkt->stream_index == si && avcodec_send_packet(ctx, pkt) >= 0) {
        while (avcodec_receive_frame(ctx, frame) >= 0) {
          const double t = double(frame->best_effort_timestamp) * tb;
          if (t + 0.05 >= target) {
            got = true;
            break;
          }
        }
      }
      av_packet_unref(pkt);
    }
    if (!got) {
      avcodec_send_packet(ctx, nullptr);
      got = avcodec_receive_frame(ctx, frame) >= 0;
    }
    if (!got) break;
    // Letterbox into the thumbnail canvas.
    const double ar = double(frame->width) / std::max(1, frame->height);
    int w = kThumbW, h = int(kThumbW / ar);
    if (h > kThumbH) {
      h = kThumbH;
      w = int(kThumbH * ar);
    }
    w = std::max(2, w & ~1);
    h = std::max(2, h & ~1);
    sws = sws_getContext(frame->width, frame->height, AVPixelFormat(frame->format), w, h, AV_PIX_FMT_BGRA, SWS_BICUBIC,
                         nullptr, nullptr, nullptr);
    if (!sws) break;
    bgra.assign(size_t(kThumbW) * kThumbH * 4, 0);
    for (size_t i = 0; i < bgra.size(); i += 4) {
      bgra[i] = 38;
      bgra[i + 1] = 31;
      bgra[i + 2] = 28;
      bgra[i + 3] = 255;
    }
    const int x0 = (kThumbW - w) / 2, y0 = (kThumbH - h) / 2;
    uint8_t* dst[1] = {bgra.data() + (size_t(y0) * kThumbW + x0) * 4};
    const int stride[1] = {kThumbW * 4};
    sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, stride);
    ok = true;
  } while (false);
  sws_freeContext(sws);
  av_frame_free(&frame);
  av_packet_free(&pkt);
  avcodec_free_context(&ctx);
  avformat_close_input(&fmt);
  return ok;
}

}  // namespace

std::optional<fs::path> makeThumbnail(const fs::path& clip, const fs::path& cacheDir) {
  if (!fs::exists(clip)) return std::nullopt;
  const fs::path out = cachePath(clip, cacheDir);
  if (fs::exists(out)) return out;
  std::vector<uint8_t> bgra;
  if (!decodeFrameAt(clip, 1.0, bgra)) return std::nullopt;
  std::error_code ec;
  fs::create_directories(cacheDir, ec);
  if (!writeImage(out, bgra.data(), kThumbW, kThumbH, kThumbW * 4, ImageFormat::Jpeg)) return std::nullopt;
  return out;
}

ThumbnailQueue::ThumbnailQueue(fs::path cacheDir) : cacheDir_(std::move(cacheDir)) {
  thread_ = std::thread([this] { loop(); });
}

ThumbnailQueue::~ThumbnailQueue() {
  {
    std::lock_guard lk(mu_);
    stop_ = true;
  }
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
}

void ThumbnailQueue::request(const fs::path& clip) {
  {
    std::lock_guard lk(mu_);
    if (!queued_.insert(clip).second) return;
    pending_.push_back(clip);
  }
  cv_.notify_all();
}

std::vector<ThumbnailQueue::Result> ThumbnailQueue::poll() {
  std::lock_guard lk(mu_);
  return std::exchange(done_, {});
}

void ThumbnailQueue::loop() {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // WIC
  while (true) {
    fs::path clip;
    {
      std::unique_lock lk(mu_);
      cv_.wait(lk, [&] { return stop_ || !pending_.empty(); });
      if (stop_) break;
      clip = pending_.front();
      pending_.pop_front();
    }
    Result r{clip, makeThumbnail(clip, cacheDir_)};
    std::lock_guard lk(mu_);
    queued_.erase(clip);
    done_.push_back(std::move(r));
  }
  CoUninitialize();
}

}  // namespace clipper
