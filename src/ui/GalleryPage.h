#pragma once
#include "gallery/ClipLibrary.h"
#include "gallery/Player.h"
#include "gallery/Thumbnailer.h"
#include "ui/Fonts.h"
#include "ui/Texture.h"
#include "ui/UiContext.h"
#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>

namespace clipper {

// Gallery grid + clip player/export view.
class GalleryPage {
public:
  explicit GalleryPage(UiContext& ctx);
  ~GalleryPage();

  void onOpen();   // window opened / page shown: rescan the folder
  void onClose();  // window closing: release textures, stop playback
  void refresh();
  void draw(const UiFonts& f);
  bool wantsAnimation() const;
  void openClip(const std::filesystem::path& clip);
  void prepareForSnapshot();  // synchronous thumbnails (offscreen previews)
  const std::vector<ClipInfo>& clips() const;

private:
  void drawGrid(const UiFonts& f);
  void drawCard(const UiFonts& f, const ClipInfo& c, float width);
  void drawContextMenu(const UiFonts& f);
  void drawRenameModal(const UiFonts& f);
  void drawPlayer(const UiFonts& f);
  void drawTracks(const UiFonts& f, float width);
  void drawExport(const UiFonts& f, float width);
  void pollThumbnails();
  void closePlayer();
  void ensureLibrary();
  void finishExport();

  UiContext& ctx_;
  std::unique_ptr<ClipLibrary> library_;
  std::unique_ptr<ThumbnailQueue> thumbQueue_;
  std::map<std::wstring, Texture> thumbs_;
  std::set<std::wstring> requested_;
  std::string query_;
  std::string message_;
  std::filesystem::path menuClip_;
  std::filesystem::path renameClip_;
  std::string renameText_;
  bool openRename_ = false;

  std::unique_ptr<Player> player_;
  std::filesystem::path playerClip_;
  Texture videoTex_;
  std::vector<uint8_t> frame_;
  bool scrubbing_ = false;
  double scrubPos_ = 0;
  double lastScrubSeek_ = 0;
  bool alsoSeparate_ = false;

  std::thread exportThread_;
  std::atomic<bool> exporting_{false};
  std::atomic<bool> exportDone_{false};
  std::atomic<float> exportProgress_{0.f};
  std::filesystem::path exportResult_;
  std::string exportError_;
};

}  // namespace clipper
