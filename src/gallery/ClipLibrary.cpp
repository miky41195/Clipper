#include "gallery/ClipLibrary.h"
#include "core/Text.h"
#include "output/ClipWriter.h"
#include "core/I18n.h"
#include <algorithm>
#include <ctime>
#include <format>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>

extern "C" {
#include <libavformat/avformat.h>
}

namespace clipper {
namespace fs = std::filesystem;
using std::chrono::system_clock;

namespace {

system_clock::time_point toSystem(fs::file_time_type t) { return std::chrono::clock_cast<system_clock>(t); }

std::string streamName(AVStream* st, int index) {
  for (const char* key : {"title", "handler_name"}) {
    const AVDictionaryEntry* e = av_dict_get(st->metadata, key, nullptr, 0);
    if (e && e->value[0] && std::string_view(e->value) != "SoundHandler") return e->value;
  }
  return std::format("Audio {}", index);
}

std::tm localTm(system_clock::time_point t) {
  const std::time_t tt = system_clock::to_time_t(t);
  std::tm lt{};
  localtime_s(&lt, &tt);
  return lt;
}

}  // namespace

std::optional<ClipInfo> probeClip(const fs::path& file) {
  AVFormatContext* fmt = nullptr;
  if (avformat_open_input(&fmt, pathToUtf8(file).c_str(), nullptr, nullptr) < 0) return std::nullopt;
  ClipInfo c;
  c.path = file;
  c.name = pathToUtf8(file.stem());
  std::error_code ec;
  c.bytes = int64_t(fs::file_size(file, ec));
  c.modified = toSystem(fs::last_write_time(file, ec));
  if (fmt->duration <= 0) avformat_find_stream_info(fmt, nullptr);
  c.seconds = fmt->duration > 0 ? double(fmt->duration) / AV_TIME_BASE : 0;
  int audioIndex = 1;
  for (unsigned i = 0; i < fmt->nb_streams; ++i) {
    AVStream* st = fmt->streams[i];
    if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) c.tracks.push_back(streamName(st, audioIndex++));
    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && c.width == 0) {
      c.width = st->codecpar->width;
      c.height = st->codecpar->height;
    }
  }
  const AVDictionaryEntry* comment = av_dict_get(fmt->metadata, "comment", nullptr, 0);
  const std::string_view tag = comment ? comment->value : "";
  c.app = tag.starts_with(kAppTagPrefix) ? std::string(tag.substr(std::string_view(kAppTagPrefix).size())) : "—";
  avformat_close_input(&fmt);
  return c;
}

ClipLibrary::ClipLibrary(fs::path dir, fs::path cacheFile) : dir_(std::move(dir)), cacheFile_(std::move(cacheFile)) { loadCache(); }

void ClipLibrary::loadCache() {
  std::ifstream in(cacheFile_, std::ios::binary);
  if (!in) return;
  const auto j = nlohmann::json::parse(in, nullptr, false);
  if (!j.is_object() || !j.contains("entries")) return;
  for (const auto& e : j["entries"]) {
    try {
      Cached c;
      c.size = e.at("size").get<int64_t>();
      c.mtime = e.at("mtime").get<int64_t>();
      c.info.path = utf8ToWide(e.at("path").get<std::string>());
      c.info.name = pathToUtf8(c.info.path.stem());
      c.info.bytes = c.size;
      c.info.seconds = e.at("seconds").get<double>();
      c.info.app = e.at("app").get<std::string>();
      c.info.tracks = e.at("tracks").get<std::vector<std::string>>();
      c.info.width = e.value("width", 0);
      c.info.height = e.value("height", 0);
      cache_.emplace_back(c.info.path.wstring(), std::move(c));
    } catch (...) {
    }
  }
}

void ClipLibrary::saveCache() const {
  nlohmann::json entries = nlohmann::json::array();
  for (const auto& [key, c] : cache_)
    entries.push_back({{"path", pathToUtf8(c.info.path)}, {"size", c.size}, {"mtime", c.mtime}, {"seconds", c.info.seconds},
                       {"app", c.info.app}, {"tracks", c.info.tracks}, {"width", c.info.width}, {"height", c.info.height}});
  std::error_code ec;
  if (cacheFile_.has_parent_path()) fs::create_directories(cacheFile_.parent_path(), ec);
  std::ofstream(cacheFile_, std::ios::binary) << nlohmann::json{{"entries", entries}}.dump();
}

const std::vector<ClipInfo>& ClipLibrary::refresh() {
  std::vector<ClipInfo> found;
  std::vector<std::pair<std::wstring, Cached>> nextCache;
  bool changed = false;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(dir_, ec)) {
    if (!e.is_regular_file(ec)) continue;
    const std::string ext = toLowerAscii(pathToUtf8(e.path().extension()));
    if (ext != ".mp4" && ext != ".mkv") continue;
    const int64_t size = int64_t(e.file_size(ec));
    const int64_t mtime = int64_t(e.last_write_time(ec).time_since_epoch().count());
    const std::wstring key = e.path().wstring();
    const auto hit = std::find_if(cache_.begin(), cache_.end(), [&](const auto& c) {
      return c.first == key && c.second.size == size && c.second.mtime == mtime;
    });
    Cached entry;
    if (hit != cache_.end()) {
      entry = hit->second;
    } else {
      ++probes_;
      const auto info = probeClip(e.path());
      if (!info) continue;
      entry = {size, mtime, *info};
      changed = true;
    }
    entry.info.modified = toSystem(e.last_write_time(ec));
    entry.info.bytes = size;
    found.push_back(entry.info);
    nextCache.emplace_back(key, std::move(entry));
  }
  if (nextCache.size() != cache_.size()) changed = true;
  cache_ = std::move(nextCache);
  if (changed) saveCache();
  std::sort(found.begin(), found.end(), [](const ClipInfo& a, const ClipInfo& b) { return a.modified > b.modified; });
  clips_ = std::move(found);
  return clips_;
}

std::vector<std::pair<std::string, std::vector<const ClipInfo*>>> groupByDay(const std::vector<ClipInfo>& clips,
                                                                             system_clock::time_point now) {
  static const char* kMonthsIt[] = {"gennaio", "febbraio", "marzo",     "aprile",  "maggio",   "giugno",
                                    "luglio",  "agosto",   "settembre", "ottobre", "novembre", "dicembre"};
  static const char* kMonthsEn[] = {"January", "February", "March",     "April",   "May",      "June",
                                    "July",    "August",   "September", "October", "November", "December"};
  const std::tm today = localTm(now);
  std::tm y = today;
  y.tm_mday -= 1;
  y.tm_isdst = -1;
  std::mktime(&y);
  const bool english = std::string_view(tr("gal.today")) == "Today";
  std::vector<std::pair<std::string, std::vector<const ClipInfo*>>> out;
  int lastKey = -1;
  for (const auto& c : clips) {
    const std::tm t = localTm(c.modified);
    const int key = (t.tm_year * 12 + t.tm_mon) * 32 + t.tm_mday;
    if (key != lastKey) {
      std::string label;
      if (t.tm_year == today.tm_year && t.tm_yday == today.tm_yday) label = tr("gal.today");
      else if (t.tm_year == y.tm_year && t.tm_yday == y.tm_yday) label = tr("gal.yesterday");
      else if (english) label = std::format("{} {}, {}", kMonthsEn[t.tm_mon], t.tm_mday, t.tm_year + 1900);
      else label = std::format("{} {} {}", t.tm_mday, kMonthsIt[t.tm_mon], t.tm_year + 1900);
      out.push_back({label, {}});
      lastKey = key;
    }
    out.back().second.push_back(&c);
  }
  return out;
}

bool matches(const ClipInfo& clip, std::string_view query) {
  if (query.empty()) return true;
  const std::string q = toLowerAscii(std::string(query));
  return toLowerAscii(clip.name).find(q) != std::string::npos || toLowerAscii(clip.app).find(q) != std::string::npos;
}

}  // namespace clipper
