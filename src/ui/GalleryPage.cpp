#include "ui/GalleryPage.h"
#include "core/SizeEstimate.h"
#include "core/Text.h"
#include "gallery/Exporter.h"
#include "gallery/FileOps.h"
#include "core/I18n.h"
#include "ui/Icons.h"
#include "ui/ImageIO.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <windows.h>
#include <combaseapi.h>
#include <ctime>
#include <format>

namespace clipper {
namespace fs = std::filesystem;
using theme::px;

namespace {
std::string timeOfDay(std::chrono::system_clock::time_point t) {
  const std::time_t tt = std::chrono::system_clock::to_time_t(t);
  std::tm lt{};
  localtime_s(&lt, &tt);
  return std::format("{:02}:{:02}", lt.tm_hour, lt.tm_min);
}

void textEllipsis(ImDrawList* dl, ImVec2 pos, float maxW, ImU32 color, const std::string& text) {
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  const float h = ImGui::GetTextLineHeight();
  ImGui::RenderTextEllipsis(dl, pos, {pos.x + maxW, pos.y + h}, pos.x + maxW, pos.x + maxW, text.c_str(), nullptr, nullptr);
  ImGui::PopStyleColor();
}
}  // namespace

GalleryPage::GalleryPage(UiContext& ctx) : ctx_(ctx) {}

GalleryPage::~GalleryPage() {
  if (exportThread_.joinable()) exportThread_.join();
  if (scanThread_.joinable()) scanThread_.join();
}

std::vector<ClipInfo> GalleryPage::clips() const {
  std::lock_guard lk(scanMu_);
  return shown_;
}

bool GalleryPage::scanning() const {
  std::lock_guard lk(scanMu_);
  return scanning_;
}

fs::path GalleryPage::clipsDir() const {
  return ctx_.clipsFolder ? ctx_.clipsFolder(ctx_.currentSettings ? ctx_.currentSettings() : Settings{}) : fs::path{};
}

void GalleryPage::ensureLibrary() {
  const fs::path cache = ctx_.thumbnailCache ? ctx_.thumbnailCache() : fs::temp_directory_path() / "clipper_thumbs";
  if (!library_) library_ = std::make_unique<ClipLibrary>(clipsDir(), cache / "library.json");
  if (!thumbQueue_) thumbQueue_ = std::make_unique<ThumbnailQueue>(cache);
}

// Scans on a worker thread (probing new files can take a while); requests during a scan coalesce into one rescan.
void GalleryPage::refresh() {
  ensureLibrary();
  {
    std::lock_guard lk(scanMu_);
    if (scanning_) {
      rescanPending_ = true;
      return;
    }
    scanning_ = true;
  }
  if (scanThread_.joinable()) scanThread_.join();
  scanThread_ = std::thread([this, dir = clipsDir()] {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    while (true) {
      library_->setDir(dir);
      std::vector<ClipInfo> result = library_->refresh();
      std::lock_guard lk(scanMu_);
      shown_ = std::move(result);
      if (!rescanPending_) {
        scanning_ = false;
        break;
      }
      rescanPending_ = false;
    }
    CoUninitialize();
  });
}

void GalleryPage::waitForScan() {
  for (int i = 0; i < 500 && scanning(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

void GalleryPage::onOpen() { refresh(); }

void GalleryPage::onClose() {
  closePlayer();
  thumbs_.clear();
  requested_.clear();
  videoTex_.reset();
  thumbQueue_.reset();
}

void GalleryPage::prepareForSnapshot() {
  refresh();
  waitForScan();
  const fs::path cache = ctx_.thumbnailCache ? ctx_.thumbnailCache() : fs::temp_directory_path() / "clipper_thumbs";
  for (const auto& c : clips()) {
    if (const auto t = makeThumbnail(c.path, cache)) {
      std::vector<uint8_t> px;
      int w = 0, h = 0;
      if (readImage(*t, px, w, h)) thumbs_[c.path.wstring()].upload(px.data(), w, h);
    }
    requested_.insert(c.path.wstring());
  }
  if (player_)
    for (int i = 0; i < 150; ++i) {
      int w = 0, h = 0;
      if (player_->takeFrame(frame_, w, h)) {
        videoTex_.upload(frame_.data(), w, h);
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

bool GalleryPage::wantsAnimation() const {
  return (player_ && player_->playing()) || exporting_ || scrubbing_ || scanning() ||
         (!requested_.empty() && thumbs_.size() < requested_.size());
}

void GalleryPage::pollThumbnails() {
  if (!thumbQueue_) return;
  for (const auto& r : thumbQueue_->poll()) {
    std::vector<uint8_t> px;
    int w = 0, h = 0;
    if (r.thumbnail && readImage(*r.thumbnail, px, w, h)) thumbs_[r.clip.wstring()].upload(px.data(), w, h);
    else thumbs_[r.clip.wstring()];  // failed: keep the placeholder, don't retry every frame
  }
}

void GalleryPage::openClip(const fs::path& clip) {
  closePlayer();
  player_ = std::make_unique<Player>(clip);
  playerClip_ = clip;
  // "Mix" duplicates the other tracks: start with it off so nothing plays twice.
  const auto names = player_->trackNames();
  if (names.size() > 1)
    for (int i = 0; i < int(names.size()); ++i)
      if (names[size_t(i)] == "Mix") player_->setTrackEnabled(i, false);
  exportResult_.clear();
  exportError_.clear();
}

void GalleryPage::closePlayer() {
  player_.reset();
  playerClip_.clear();
  videoTex_.reset();
  scrubbing_ = false;
}

void GalleryPage::draw(const UiFonts& f) {
  ensureLibrary();
  pollThumbnails();
  finishExport();
  if (player_) drawPlayer(f);
  else drawGrid(f);
  drawRenameModal(f);
}

void GalleryPage::drawGrid(const UiFonts& f) {
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {px(40), px(30)});
  ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
  ImGui::BeginChild("##gallery", avail, ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PopStyleColor();

  const std::vector<ClipInfo> all = clips();
  int64_t total = 0;
  for (const auto& c : all) total += c.bytes;
  const float headerY = ImGui::GetCursorPosY();
  ui::pageHeader(f, tr("nav.gallery"), trf("gal.count", all.size(), formatBytes(uint64_t(total))).c_str());
  const float afterHeader = ImGui::GetCursorPosY();

  // Search + actions, right aligned on the title line.
  const float btn = ImGui::GetFrameHeight();
  const float searchW = px(260);
  const float right = ImGui::GetWindowWidth() - px(40);
  ImGui::SetCursorPos({right - searchW - btn * 2 - px(16), headerY + px(4)});
  const ImVec2 sp = ImGui::GetCursorScreenPos();
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {px(34), px(7)});
  ImGui::SetNextItemWidth(searchW);
  ImGui::InputTextWithHint("##search", tr("gal.search"), &query_);
  ImGui::PopStyleVar();
  ImGui::GetWindowDrawList()->AddText(f.icon, f.icon->FontSize * 0.85f, {sp.x + px(11), sp.y + px(8)}, theme::kMuted, icon::kSearch);
  ImGui::SameLine(0, px(8));
  if (ui::iconButton(f, "##folder", icon::kOpenFolder, tr("gal.folder")) && ctx_.openFolder) ctx_.openFolder(clipsDir());
  ImGui::SameLine(0, px(8));
  if (ui::iconButton(f, "##refresh", icon::kRefresh, tr("common.refresh"))) refresh();
  ImGui::SetCursorPosY(afterHeader);

  if (!message_.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::accent());
    ImGui::TextUnformatted((std::string(icon::kCheck) + "  " + message_).c_str());
    ImGui::PopStyleColor();
  }

  std::vector<ClipInfo> shown;
  for (const auto& c : all)
    if (matches(c, query_)) shown.push_back(c);
  if (all.empty() || shown.empty()) {
    const std::string text =
        all.empty() ? trf("gal.empty", ctx_.currentSettings ? ctx_.currentSettings().hotkeys.saveClip : std::string("Alt+F10"))
                    : std::string(tr("gal.noresults"));
    const ImVec2 a = ImGui::GetContentRegionAvail();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + a.y * 0.3f);
    ImGui::PushFont(f.icon);
    const ImVec2 is = ImGui::CalcTextSize(icon::kGallery);
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - is.x) * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::kFaint);
    ImGui::TextUnformatted(icon::kGallery);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    const float w = std::min(a.x, px(440));
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - w) * 0.5f);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::kMuted);
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
  } else {
    const float gap = px(18);
    const float width = ImGui::GetContentRegionAvail().x;
    const int cols = std::max(1, int((width + gap) / (px(250) + gap)));
    const float cardW = (width - gap * float(cols - 1)) / float(cols);
    for (const auto& [label, items] : groupByDay(shown, std::chrono::system_clock::now())) {
      ImGui::Dummy({0, px(4)});
      ImGui::PushFont(f.bold);
      ImGui::PushStyleColor(ImGuiCol_Text, theme::kMuted);
      ImGui::TextUnformatted(label.c_str());
      ImGui::PopStyleColor();
      ImGui::PopFont();
      ImGui::Dummy({0, px(2)});
      for (size_t i = 0; i < items.size(); ++i) {
        if (i % size_t(cols) != 0) ImGui::SameLine(0, gap);
        drawCard(f, *items[i], cardW);
        if (i % size_t(cols) == size_t(cols) - 1 || i + 1 == items.size()) ImGui::Dummy({0, px(6)});
      }
    }
  }
  drawContextMenu(f);
  ImGui::EndChild();
  ImGui::PopStyleVar();
}

void GalleryPage::drawCard(const UiFonts& f, const ClipInfo& c, float w) {
  const std::wstring key = c.path.wstring();
  if (!requested_.contains(key) && thumbQueue_) {
    requested_.insert(key);
    thumbQueue_->request(c.path);
  }
  const float thumbH = std::floor(w * 9.f / 16.f);
  const float infoH = px(62);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::PushID(pathToUtf8(c.path).c_str());
  const bool clicked = ImGui::InvisibleButton("##card", {w, thumbH + infoH});
  const bool hovered = ImGui::IsItemHovered();
  if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
    menuClip_ = c.path;
    ImGui::OpenPopup("##clipmenu");
  }
  ImGui::PopID();
  if (clicked) openClip(c.path);
  if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float r = px(10);
  dl->AddRectFilled(p, {p.x + w, p.y + thumbH + infoH}, hovered ? theme::kCardHover : theme::kCard, r);
  const ImVec2 t0 = p, t1{p.x + w, p.y + thumbH};
  const auto it = thumbs_.find(key);
  if (it != thumbs_.end() && it->second.valid()) {
    dl->AddImageRounded(it->second.id(), t0, t1, {0, 0}, {1, 1}, IM_COL32_WHITE, r, ImDrawFlags_RoundCornersTop);
  } else {
    dl->AddRectFilled(t0, t1, IM_COL32(36, 40, 49, 255), r, ImDrawFlags_RoundCornersTop);
    ImGui::PushFont(f.icon);
    const ImVec2 is = ImGui::CalcTextSize(icon::kVideo);
    dl->AddText({p.x + (w - is.x) * 0.5f, p.y + (thumbH - is.y) * 0.5f}, theme::kFaint, icon::kVideo);
    ImGui::PopFont();
  }
  if (hovered) {
    dl->AddRectFilled(t0, t1, IM_COL32(0, 0, 0, 70), r, ImDrawFlags_RoundCornersTop);
    const ImVec2 cc{p.x + w * 0.5f, p.y + thumbH * 0.5f};
    dl->AddCircleFilled(cc, px(24), theme::accent(), 32);
    ImGui::PushFont(f.icon);
    const ImVec2 is = ImGui::CalcTextSize(icon::kPlay);
    dl->AddText({cc.x - is.x * 0.5f + px(2), cc.y - is.y * 0.5f}, theme::kBg, icon::kPlay);
    ImGui::PopFont();
  }
  // Duration badge
  ImGui::PushFont(f.smallText);
  const std::string dur = ui::clipTime(c.seconds);
  const ImVec2 ds = ImGui::CalcTextSize(dur.c_str());
  const ImVec2 b1{t1.x - px(8), t1.y - px(8)}, b0{b1.x - ds.x - px(12), b1.y - ds.y - px(4)};
  dl->AddRectFilled(b0, b1, IM_COL32(0, 0, 0, 170), px(5));
  dl->AddText({b0.x + px(6), b0.y + px(2)}, theme::kText, dur.c_str());
  ImGui::PopFont();
  // Info
  ImGui::PushFont(f.bold);
  textEllipsis(dl, {p.x + px(12), p.y + thumbH + px(10)}, w - px(24), theme::kText, c.name);
  ImGui::PopFont();
  ImGui::PushFont(f.smallText);
  const std::string meta = std::format("{}  ·  {}  ·  {}", c.app, formatBytes(uint64_t(c.bytes)), timeOfDay(c.modified));
  textEllipsis(dl, {p.x + px(12), p.y + thumbH + px(34)}, w - px(24), theme::kMuted, meta);
  ImGui::PopFont();
}

void GalleryPage::drawContextMenu(const UiFonts& f) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {px(6), px(6)});
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {px(8), px(4)});
  if (ImGui::BeginPopup("##clipmenu")) {
    auto item = [&](const char* glyph, const char* label, ImU32 color = theme::kText) {
      ImGui::PushStyleColor(ImGuiCol_Text, color);
      const bool r = ImGui::Selectable((std::string(glyph) + "   " + label).c_str(), false, 0, {px(220), ImGui::GetFrameHeight()});
      ImGui::PopStyleColor();
      return r;
    };
    if (item(icon::kPlay, tr("common.open"))) openClip(menuClip_);
    if (item(icon::kRename, tr("gal.rename"))) {
      renameClip_ = menuClip_;
      renameText_ = pathToUtf8(menuClip_.stem());
      openRename_ = true;
    }
    if (item(icon::kOpenFolder, tr("gal.reveal"))) revealInExplorer(menuClip_);
    if (item(icon::kCopy, tr("gal.copy")) && copyFileToClipboard(menuClip_, nullptr)) message_ = tr("gal.copied");
    ImGui::Separator();
    if (item(icon::kDelete, tr("gal.delete"), theme::kDanger)) {
      if (recycle(menuClip_)) {
        message_ = tr("gal.deleted");
        refresh();
      }
    }
    ImGui::EndPopup();
  }
  ImGui::PopStyleVar(2);
  (void)f;
}

void GalleryPage::drawRenameModal(const UiFonts& f) {
  if (openRename_) {
    ImGui::OpenPopup("##rename");
    openRename_ = false;
  }
  const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, {0.5f, 0.5f});
  ImGui::SetNextWindowSize({px(420), 0});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {px(22), px(20)});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(12));
  if (ImGui::BeginPopupModal("##rename", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize)) {
    ImGui::PushFont(f.bold);
    ImGui::TextUnformatted(tr("gal.rename"));
    ImGui::PopFont();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    const bool enter = ImGui::InputText("##newname", &renameText_, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Dummy({0, px(6)});
    const float bw = px(110);
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - px(22) - bw * 2 - px(10));
    if (ui::secondaryButton(tr("btn.revert"), true, bw) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::SameLine(0, px(10));
    if (ui::primaryButton(tr("gal.rename"), !renameText_.empty(), bw) || enter) {
      try {
        const auto renamed = renameClip(renameClip_, renameText_);
        if (playerClip_ == renameClip_) playerClip_ = renamed;
        message_ = tr("gal.renamed");
        refresh();
      } catch (const std::exception& e) {
        message_ = e.what();
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  ImGui::PopStyleVar(2);
}

void GalleryPage::drawPlayer(const UiFonts& f) {
  Player& pl = *player_;
  int w = 0, h = 0;
  if (pl.takeFrame(frame_, w, h)) videoTex_.upload(frame_.data(), w, h);

  const ImVec2 avail = ImGui::GetContentRegionAvail();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {px(32), px(24)});
  ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
  ImGui::BeginChild("##player", avail, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
  ImGui::PopStyleColor();

  // Top bar
  if (ui::secondaryButton((std::string(icon::kBack) + "  " + tr("pl.back")).c_str())) {
    closePlayer();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    return;
  }
  ImGui::SameLine(0, px(16));
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(4));
  ImGui::PushFont(f.title);
  ImGui::TextUnformatted(pathToUtf8(playerClip_.stem()).c_str());
  ImGui::PopFont();
  ImGui::SameLine(ImGui::GetWindowWidth() - px(32) - ImGui::GetFrameHeight() * 2 - px(8));
  if (ui::iconButton(f, "##reveal", icon::kOpenFolder, tr("gal.reveal"))) revealInExplorer(playerClip_);
  ImGui::SameLine(0, px(8));
  if (ui::iconButton(f, "##copy", icon::kCopy, tr("gal.copy")) && copyFileToClipboard(playerClip_, nullptr)) message_ = tr("gal.copied");
  ImGui::Dummy({0, px(6)});

  if (!pl.ok()) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::kDanger);
    ImGui::TextUnformatted((std::string(icon::kWarning) + "  " + tr("pl.error") + ": " + pl.error()).c_str());
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    return;
  }

  const float gap = px(24);
  const float rightW = px(320);
  const float leftW = ImGui::GetContentRegionAvail().x - rightW - gap;
  const float controlsH = px(52);
  const float maxVideoH = ImGui::GetContentRegionAvail().y - controlsH - px(8);
  const float aspect = videoTex_.valid() ? float(videoTex_.width()) / float(videoTex_.height()) : 16.f / 9.f;
  float vw = leftW, vh = leftW / aspect;
  if (vh > maxVideoH) {
    vh = maxVideoH;
    vw = vh * aspect;
  }

  ImGui::BeginGroup();
  const ImVec2 vp = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(vp, {vp.x + leftW, vp.y + vh}, IM_COL32(0, 0, 0, 255), px(12));
  const float ox = vp.x + (leftW - vw) * 0.5f;
  if (videoTex_.valid()) dl->AddImageRounded(videoTex_.id(), {ox, vp.y}, {ox + vw, vp.y + vh}, {0, 0}, {1, 1}, IM_COL32_WHITE, px(12));
  if (ImGui::InvisibleButton("##video", {leftW, vh})) pl.playing() ? pl.pause() : pl.play();
  if (!pl.playing()) {
    const ImVec2 cc{vp.x + leftW * 0.5f, vp.y + vh * 0.5f};
    dl->AddCircleFilled(cc, px(30), IM_COL32(0, 0, 0, 140), 32);
    ImGui::PushFont(f.icon);
    const ImVec2 is = ImGui::CalcTextSize(icon::kPlay);
    dl->AddText({cc.x - is.x * 0.5f + px(2), cc.y - is.y * 0.5f}, theme::kText, icon::kPlay);
    ImGui::PopFont();
  }

  // Transport
  ImGui::Dummy({0, px(6)});
  if (ui::iconButton(f, "##pp", pl.playing() ? icon::kPause : icon::kPlay)) pl.playing() ? pl.pause() : pl.play();
  ImGui::SameLine(0, px(10));
  const double pos = scrubbing_ ? scrubPos_ : pl.position();
  const std::string times = ui::clipTime(pos) + " / " + ui::clipTime(pl.duration());
  ImGui::PushFont(f.smallText);
  const float timesW = ImGui::CalcTextSize("00:00 / 00:00").x;
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(8));
  ImGui::TextUnformatted(times.c_str());
  ImGui::PopFont();
  ImGui::SameLine(0, px(14));
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(2));
  double value = pos;
  bool released = false;
  const float tlW = leftW - ImGui::GetFrameHeight() - timesW - px(30);
  if (ui::timeline("##timeline", &value, pl.duration(), std::max(px(60), tlW), &released)) {
    scrubbing_ = true;
    scrubPos_ = value;
    if (ImGui::GetTime() - lastScrubSeek_ > 0.15) {
      pl.seek(value);
      lastScrubSeek_ = ImGui::GetTime();
    }
  }
  if (released) {
    pl.seek(scrubPos_);
    scrubbing_ = false;
  }
  ImGui::EndGroup();

  // Keyboard
  if (!ImGui::GetIO().WantTextInput) {
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) pl.playing() ? pl.pause() : pl.play();
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) pl.seek(pl.position() - 5);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) pl.seek(pl.position() + 5);
  }

  ImGui::SameLine(0, gap);
  ImGui::BeginGroup();
  drawTracks(f, rightW);
  drawExport(f, rightW);
  ImGui::EndGroup();

  ImGui::EndChild();
  ImGui::PopStyleVar();
}

void GalleryPage::drawTracks(const UiFonts& f, float width) {
  Player& pl = *player_;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {px(18), px(16)});
  ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::kCard);
  ImGui::BeginChild("##tracks", {width, 0}, ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders);
  ui::cardTitle(f, tr("pl.tracks"), tr("pl.tracks.hint"));
  const auto names = pl.trackNames();
  if (names.empty()) ui::mutedText(f, tr("pl.notracks"));
  const float inner = ImGui::GetContentRegionAvail().x;
  for (int i = 0; i < int(names.size()); ++i) {
    ImGui::PushID(i);
    bool on = pl.trackEnabled(i);
    const float y0 = ImGui::GetCursorPosY();
    ImGui::PushStyleColor(ImGuiCol_Text, on ? theme::kText : theme::kFaint);
    ImGui::TextUnformatted(names[size_t(i)].c_str());
    ImGui::PopStyleColor();
    const std::string pct = std::format("{}%", int(std::lround(pl.trackGain(i) * 100)));
    ImGui::SameLine();
    ImGui::SetCursorPos({ImGui::GetCursorPosX(), y0});
    ImGui::SetCursorPosX(ImGui::GetStyle().WindowPadding.x + inner - px(40) - ImGui::CalcTextSize(pct.c_str()).x - px(10));
    ImGui::PushStyleColor(ImGuiCol_Text, theme::kMuted);
    ImGui::TextUnformatted(pct.c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::SetCursorPosY(y0 - px(1));
    ImGui::SetCursorPosX(ImGui::GetStyle().WindowPadding.x + inner - px(40));
    {
      // compact toggle
      const float th = px(20), tw = px(36);
      const ImVec2 p = ImGui::GetCursorScreenPos();
      if (ImGui::InvisibleButton("##on", {tw, th})) pl.setTrackEnabled(i, !on);
      on = pl.trackEnabled(i);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(p, {p.x + tw, p.y + th}, on ? theme::accent() : theme::kControl, th * 0.5f);
      dl->AddCircleFilled({on ? p.x + tw - th * 0.5f : p.x + th * 0.5f, p.y + th * 0.5f}, th * 0.5f - px(3), on ? theme::kBg : theme::kText, 16);
    }
    float gain = pl.trackGain(i);
    ImGui::BeginDisabled(!on);
    if (ui::smallSlider("##gain", &gain, 0.f, 2.f, inner)) pl.setTrackGain(i, gain);
    ImGui::EndDisabled();
    // level meter
    const ImVec2 mp = ImGui::GetCursorScreenPos();
    const float lvl = on ? std::min(1.f, pl.trackLevel(i) * pl.trackGain(i)) : 0.f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(mp, {mp.x + inner, mp.y + px(3)}, IM_COL32(255, 255, 255, 12), px(2));
    dl->AddRectFilled(mp, {mp.x + inner * lvl, mp.y + px(3)}, theme::accentAlpha(170), px(2));
    ImGui::Dummy({inner, px(10)});
    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
  ImGui::Dummy({0, px(6)});
}

void GalleryPage::drawExport(const UiFonts& f, float width) {
  Player& pl = *player_;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {px(18), px(16)});
  ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::kCard);
  ImGui::BeginChild("##export", {width, 0}, ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders);
  ui::cardTitle(f, tr("pl.export"), tr("pl.export.hint"));
  const float inner = ImGui::GetContentRegionAvail().x;
  {
    const float y = ImGui::GetCursorPosY();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inner - px(52));
    ImGui::TextWrapped("%s", tr("pl.separate"));
    ImGui::PopTextWrapPos();
    const float after = ImGui::GetCursorPosY();
    ImGui::SetCursorPos({ImGui::GetStyle().WindowPadding.x + inner - px(40), y});
    ui::switchButton("##separate", &alsoSeparate_);
    ImGui::SetCursorPosY(std::max(after, y + px(26)));
  }
  ImGui::Dummy({0, px(2)});
  if (exporting_) {
    ui::progressBar(exportProgress_, inner, px(6));
    ui::mutedText(f, trf("pl.exporting", int(exportProgress_ * 100)), false);
  } else {
    ImGui::PushFont(f.bold);
    if (ui::primaryButton((std::string(icon::kExport) + "  " + tr("pl.export")).c_str(), true, inner)) {
      ExportOptions opt;
      const int n = int(pl.trackNames().size());
      for (int i = 0; i < n; ++i) {
        opt.keep.push_back(pl.trackEnabled(i));
        opt.gain.push_back(pl.trackGain(i));
      }
      opt.alsoSeparate = alsoSeparate_;
      exporting_ = true;
      exportDone_ = false;
      exportProgress_ = 0;
      exportResult_.clear();
      exportError_.clear();
      if (exportThread_.joinable()) exportThread_.join();
      exportThread_ = std::thread([this, src = playerClip_, opt] {
        try {
          exportResult_ = exportClip(src, opt, [this](float p) { exportProgress_ = p; });
        } catch (const std::exception& e) {
          exportError_ = e.what();
        }
        exportDone_ = true;
      });
    }
    ImGui::PopFont();
    if (!exportResult_.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, theme::accent());
      ImGui::TextWrapped("%s", (std::string(icon::kCheck) + "  " + trf("pl.exported", pathToUtf8(exportResult_.filename()))).c_str());
      ImGui::PopStyleColor();
      if (ui::secondaryButton(tr("gal.reveal"), true, inner)) revealInExplorer(exportResult_);
    }
    if (!exportError_.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, theme::kDanger);
      ImGui::TextWrapped("%s", (std::string(icon::kWarning) + "  " + exportError_).c_str());
      ImGui::PopStyleColor();
    }
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
}

void GalleryPage::finishExport() {
  if (!exportDone_) return;
  if (exportThread_.joinable()) exportThread_.join();
  exportDone_ = false;
  exporting_ = false;
  refresh();
}

}  // namespace clipper
