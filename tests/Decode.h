#pragma once
#include <cmath>
#include <filesystem>
#include "core/Text.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

// Time (s) of the first decoded frame/sample matching a predicate, or -1.
inline double firstEventTime(const std::filesystem::path& file, AVMediaType type) {
  AVFormatContext* fmt = nullptr;
  if (avformat_open_input(&fmt, clipper::pathToUtf8(file).c_str(), nullptr, nullptr) < 0) return -1;
  avformat_find_stream_info(fmt, nullptr);
  const AVCodec* codec = nullptr;
  const int si = av_find_best_stream(fmt, type, -1, -1, &codec, 0);
  double result = -1;
  if (si >= 0) {
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, fmt->streams[si]->codecpar);
    avcodec_open2(ctx, codec, nullptr);
    const double tb = av_q2d(fmt->streams[si]->time_base);
    AVPacket* pkt = av_packet_alloc();
    AVFrame* fr = av_frame_alloc();
    auto scan = [&]() {
      while (avcodec_receive_frame(ctx, fr) >= 0) {
        const double t0 = double(fr->best_effort_timestamp) * tb;
        if (type == AVMEDIA_TYPE_VIDEO) {
          uint64_t sum = 0;
          for (int y = 0; y < fr->height; y += 4)
            for (int x = 0; x < fr->width; x += 4) sum += fr->data[0][y * fr->linesize[0] + x];
          const double mean = double(sum) / ((fr->height / 4) * (fr->width / 4));
          if (mean > 128) return t0;
        } else {
          const auto* s = reinterpret_cast<const float*>(fr->data[0]);  // fltp, first channel
          for (int i = 0; i < fr->nb_samples; ++i)
            if (std::fabs(s[i]) > 0.3f) return t0 + double(i) / fr->sample_rate;
        }
      }
      return -1.0;
    };
    while (result < 0 && av_read_frame(fmt, pkt) >= 0) {
      if (pkt->stream_index == si && avcodec_send_packet(ctx, pkt) >= 0) result = scan();
      av_packet_unref(pkt);
    }
    if (result < 0) {
      avcodec_send_packet(ctx, nullptr);
      result = scan();
    }
    av_frame_free(&fr);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
  }
  avformat_close_input(&fmt);
  return result;
}
