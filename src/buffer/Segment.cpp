#include "buffer/Segment.h"
#include "core/Text.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace clipper {
namespace {
constexpr char kMagic[4] = {'C', 'L', 'S', 'G'};
constexpr uint32_t kVersion = 1;
using FilePtr = std::unique_ptr<FILE, int (*)(FILE*)>;
}  // namespace

Segment::Segment(Ticks start) : start_(start) {}

Segment::~Segment() {
  std::error_code ec;
  if (!file_.empty()) std::filesystem::remove(file_, ec);
}

void Segment::add(Packet p) {
  std::lock_guard lk(mu_);
  tracks_.insert(p.track);
  ramBytes_ += p.data ? p.data->size() : 0;
  packets_.push_back(std::move(p));
}

bool Segment::spill(const std::filesystem::path& file) {
  std::vector<Packet> copy;
  {
    std::lock_guard lk(mu_);
    if (!file_.empty()) return true;
    copy = packets_;
  }
  std::vector<char> vbuf(1 << 20);
  bool ok = false;
  {
    FilePtr f(_wfopen(file.c_str(), L"wb"), &fclose);
    if (!f) return false;
    setvbuf(f.get(), vbuf.data(), _IOFBF, vbuf.size());
    ok = fwrite(kMagic, 1, 4, f.get()) == 4 && fwrite(&kVersion, 4, 1, f.get()) == 1;
    for (const auto& p : copy) {
      if (!ok) break;
      const int32_t track = p.track;
      const uint8_t key = p.keyframe ? 1 : 0;
      const uint32_t size = p.data ? uint32_t(p.data->size()) : 0;
      ok = fwrite(&track, 4, 1, f.get()) == 1 && fwrite(&p.pts, 8, 1, f.get()) == 1 &&
           fwrite(&p.duration, 8, 1, f.get()) == 1 && fwrite(&key, 1, 1, f.get()) == 1 &&
           fwrite(&size, 4, 1, f.get()) == 1 && (size == 0 || fwrite(p.data->data(), 1, size, f.get()) == size);
    }
    ok = (fclose(f.release()) == 0) && ok;
  }
  if (!ok) {
    std::error_code ec;
    std::filesystem::remove(file, ec);
    return false;
  }
  std::lock_guard lk(mu_);
  file_ = file;
  packets_.clear();
  packets_.shrink_to_fit();
  ramBytes_ = 0;
  return true;
}

void Segment::forEach(const std::function<void(const Packet&)>& fn) const {
  std::vector<Packet> copy;
  std::filesystem::path file;
  {
    std::lock_guard lk(mu_);
    if (file_.empty()) copy = packets_;
    else file = file_;
  }
  if (file.empty()) {
    for (const auto& p : copy) fn(p);
    return;
  }
  FilePtr f(_wfopen(file.c_str(), L"rb"), &fclose);
  if (!f) throw std::runtime_error("segmento non leggibile: " + pathToUtf8(file));
  std::vector<char> vbuf(1 << 20);
  setvbuf(f.get(), vbuf.data(), _IOFBF, vbuf.size());
  char magic[4];
  uint32_t ver = 0;
  bool ok = fread(magic, 1, 4, f.get()) == 4 && std::memcmp(magic, kMagic, 4) == 0 && fread(&ver, 4, 1, f.get()) == 1 &&
            ver == kVersion;
  while (ok) {
    int32_t track = 0;
    if (fread(&track, 4, 1, f.get()) != 1) break;
    Packet p;
    uint8_t key = 0;
    uint32_t size = 0;
    ok = fread(&p.pts, 8, 1, f.get()) == 1 && fread(&p.duration, 8, 1, f.get()) == 1 &&
         fread(&key, 1, 1, f.get()) == 1 && fread(&size, 4, 1, f.get()) == 1;
    if (!ok) break;
    auto data = std::make_shared<std::vector<uint8_t>>(size);
    if (size && fread(data->data(), 1, size, f.get()) != size) {
      ok = false;
      break;
    }
    p.track = track;
    p.keyframe = key != 0;
    p.data = std::move(data);
    fn(p);
  }
  if (!ok) throw std::runtime_error("segmento corrotto: " + pathToUtf8(file));
}

size_t Segment::ramBytes() const {
  std::lock_guard lk(mu_);
  return ramBytes_;
}

bool Segment::hasTrack(int id) const {
  std::lock_guard lk(mu_);
  return tracks_.contains(id);
}

std::set<int> Segment::trackIds() const {
  std::lock_guard lk(mu_);
  return tracks_;
}

}  // namespace clipper
