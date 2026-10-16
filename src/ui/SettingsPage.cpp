#include "ui/SettingsPage.h"
#include "app/ClipNaming.h"
#include "core/SizeEstimate.h"
#include "core/Text.h"
#include "ui/AudioApps.h"
#include "ui/Color.h"
#include "ui/HotkeyCapture.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include <imgui.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <chrono>
#include <format>

namespace clipper {
using theme::px;

namespace {

const char* kAccents[] = {"#3DDC84", "#4C8DFF", "#8B5CF6", "#EC4899", "#F59E0B", "#EF4444"};

template <class T>
int indexOf(const std::vector<T>& v, const T& x, int fallback = 0) {
  const auto it = std::find(v.begin(), v.end(), x);
  return it == v.end() ? fallback : int(it - v.begin());
}

unsigned vkFromImGuiKey(ImGuiKey k) {
  if (k >= ImGuiKey_A && k <= ImGuiKey_Z) return 'A' + unsigned(k - ImGuiKey_A);
  if (k >= ImGuiKey_0 && k <= ImGuiKey_9) return '0' + unsigned(k - ImGuiKey_0);
  if (k >= ImGuiKey_F1 && k <= ImGuiKey_F24) return 0x70 + unsigned(k - ImGuiKey_F1);
  if (k >= ImGuiKey_Keypad0 && k <= ImGuiKey_Keypad9) return 0x60 + unsigned(k - ImGuiKey_Keypad0);
  switch (k) {
    case ImGuiKey_Space: return 0x20;
    case ImGuiKey_Enter: return 0x0D;
    case ImGuiKey_Tab: return 0x09;
    case ImGuiKey_Insert: return 0x2D;
    case ImGuiKey_Home: return 0x24;
    case ImGuiKey_End: return 0x23;
    case ImGuiKey_PageUp: return 0x21;
    case ImGuiKey_PageDown: return 0x22;
    case ImGuiKey_LeftArrow: return 0x25;
    case ImGuiKey_UpArrow: return 0x26;
    case ImGuiKey_RightArrow: return 0x27;
    case ImGuiKey_DownArrow: return 0x28;
    case ImGuiKey_PrintScreen: return 0x2C;
    case ImGuiKey_Pause: return 0x13;
    default: return 0;
  }
}

bool isModifier(ImGuiKey k) {
  return k == ImGuiKey_LeftCtrl || k == ImGuiKey_RightCtrl || k == ImGuiKey_LeftAlt || k == ImGuiKey_RightAlt ||
         k == ImGuiKey_LeftShift || k == ImGuiKey_RightShift || k == ImGuiKey_LeftSuper || k == ImGuiKey_RightSuper ||
         k == ImGuiKey_ReservedForModCtrl || k == ImGuiKey_ReservedForModAlt || k == ImGuiKey_ReservedForModShift ||
         k == ImGuiKey_ReservedForModSuper;
}

}  // namespace

SettingsPage::SettingsPage(UiContext& ctx) : ctx_(ctx), form_(ctx.currentSettings ? ctx.currentSettings() : Settings{}) {
  onShow();
}

void SettingsPage::onShow() {
  if (!form_.dirty() && ctx_.currentSettings) form_ = SettingsForm(ctx_.currentSettings());
  autostartSaved_ = ctx_.autostart ? ctx_.autostart() : false;
  autostartDraft_ = autostartSaved_;
  refreshLists();
}

void SettingsPage::refreshLists() {
  if (ctx_.monitors) monitors_ = ctx_.monitors();
  if (ctx_.microphones) mics_ = ctx_.microphones();
  if (ctx_.liveAudioApps) apps_ = ctx_.liveAudioApps();
}

bool SettingsPage::dirty() const { return form_.dirty() || autostartDraft_ != autostartSaved_; }

bool SettingsPage::applyDraft() {
  stopCapture();
  form_.draft = sanitize(form_.draft);
  const std::string err = ctx_.applySettings ? ctx_.applySettings(form_.draft) : std::string{};
  if (!err.empty()) {
    error_ = err;
    message_.clear();
    return false;
  }
  if (autostartDraft_ != autostartSaved_ && ctx_.setAutostart) ctx_.setAutostart(autostartDraft_);
  autostartSaved_ = autostartDraft_;
  form_.markSaved();
  setLanguage(form_.saved.language);
  if (const auto c = parseHexColor(form_.saved.accent); c && ImGui::GetCurrentContext()) theme::apply(*c, theme::scale());
  error_.clear();
  message_ = tr("footer.saved");
  return true;
}

std::pair<int, int> SettingsPage::captureSize() const {
  const MonitorInfo* m = pickMonitor(monitors_, form_.draft.monitorIndex);
  return m ? std::pair{m->width, m->height} : std::pair{1920, 1080};
}

int SettingsPage::audioTrackCount() const {
  const auto& d = form_.draft;
  int n = (d.mixTrack ? 1 : 0) + (d.captureMic ? 1 : 0);
  for (const auto& a : apps_)
    if (std::find(d.audioBlacklist.begin(), d.audioBlacklist.end(), a.exe) == d.audioBlacklist.end()) ++n;
  return n;
}

void SettingsPage::draw(const UiFonts& f, SettingsSection section) {
  const float footerH = px(68);
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {px(40), px(30)});
  ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
  ImGui::BeginChild("##settings", {avail.x, avail.y - footerH}, ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PopStyleColor();
  const float colW = std::min(ImGui::GetContentRegionAvail().x, px(760));
  ImGui::BeginChild("##col", {colW, 0}, ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
  switch (section) {
    case SettingsSection::Capture: drawCapture(f); break;
    case SettingsSection::Replay: drawReplay(f); break;
    case SettingsSection::Audio: drawAudio(f); break;
    case SettingsSection::Hotkeys: drawHotkeys(f); break;
    case SettingsSection::Output: drawOutput(f); break;
    case SettingsSection::General: drawGeneral(f); break;
  }
  ImGui::EndChild();
  ImGui::EndChild();
  ImGui::PopStyleVar();
  drawFooter(f, footerH);
}

void SettingsPage::drawCapture(const UiFonts& f) {
  auto& d = form_.draft;
  ui::pageHeader(f, tr("sec.capture"), tr("sec.capture.desc"));
  ui::beginCard("capture");

  std::vector<std::string> monNames;
  for (const auto& m : monitors_)
    monNames.push_back(std::format("Monitor {}  ·  {}×{}{}", m.index + 1, m.width, m.height,
                                   m.primary ? std::string("  ·  ") + tr("cap.primary") : std::string()));
  int mi = std::clamp(d.monitorIndex, 0, std::max(0, int(monNames.size()) - 1));
  ui::beginRow(f, tr("cap.monitor"));
  if (ui::combo("##monitor", monNames, &mi)) d.monitorIndex = mi;
  ui::endRow();

  const auto [cw, ch] = captureSize();
  std::vector<int> heights{0};
  std::vector<std::string> resLabels{tr("cap.native")};
  for (int h : {2160, 1440, 1080, 720})
    if (h < ch) {
      heights.push_back(h);
      resLabels.push_back(h == 2160 ? "4K" : std::format("{}p", h));
    }
  int ri = indexOf(heights, d.outputHeight);
  const auto [ow, oh] = computeOutputSize(cw, ch, heights[size_t(ri)]);
  const std::string outHint = trf("cap.output", ow, oh);
  ui::beginRow(f, tr("cap.resolution"), outHint.c_str());
  if (ui::segmented("##res", resLabels, &ri)) d.outputHeight = heights[size_t(ri)];
  ui::endRow();

  static const std::vector<int> fpsValues{30, 60, 120, 144};
  int fi = indexOf(fpsValues, d.fps, 1);
  ui::beginRow(f, tr("cap.fps"));
  if (ui::segmented("##fps", {"30", "60", "120", "144"}, &fi)) d.fps = fpsValues[size_t(fi)];
  ui::endRow();

  static const std::vector<Codec> codecs{Codec::AV1, Codec::HEVC, Codec::H264};
  static const char* codecHints[] = {"cap.codec.av1", "cap.codec.hevc", "cap.codec.h264"};
  int ci = indexOf(codecs, d.codec);
  ui::beginRow(f, tr("cap.codec"), tr(codecHints[ci]));
  if (ui::segmented("##codec", {"AV1", "HEVC", "H.264"}, &ci)) d.codec = codecs[size_t(ci)];
  ui::endRow();

  static const std::vector<Quality> qualities{Quality::Low, Quality::Medium, Quality::High, Quality::Custom};
  int qi = indexOf(qualities, d.quality, 2);
  const std::string kbpsHint = std::format("{} Mbps", videoKbpsFor(d, cw, ch) / 1000.0);
  ui::beginRow(f, tr("cap.quality"), kbpsHint.c_str());
  if (ui::segmented("##quality", {tr("q.low"), tr("q.medium"), tr("q.high"), tr("q.custom")}, &qi)) {
    d.quality = qualities[size_t(qi)];
    if (d.quality == Quality::Custom) d.customKbps = videoKbpsFor(Settings{d}, cw, ch);
  }
  ui::endRow();

  if (d.quality == Quality::Custom) {
    static const std::vector<int> steps{2000, 4000, 6000, 8000, 10000, 12000, 15000, 20000, 25000, 30000, 40000, 50000, 60000, 80000, 100000};
    ui::beginRow(f, tr("cap.bitrate"));
    ui::stepSlider("##kbps", steps, &d.customKbps, std::format("{} Mbps", d.customKbps / 1000));
    ui::endRow();
  }

  ui::beginRow(f, tr("cap.cursor"));
  ui::toggle("##cursor", &d.captureCursor);
  ui::endRow(false);
  ui::endCard();
  drawEstimate(f);
}

void SettingsPage::drawEstimate(const UiFonts& f) {
  const auto& d = form_.draft;
  const auto [cw, ch] = captureSize();
  const int tracks = audioTrackCount();
  const int kbps = videoKbpsFor(d, cw, ch);
  const auto clip = estimateBytes({kbps, d.audioKbps, tracks, double(d.replaySeconds)});
  const auto perMin = estimateBytes({kbps, d.audioKbps, tracks, 60.0});
  const auto buf = estimateBuffer({kbps, d.audioKbps, tracks, double(d.replaySeconds)}, d.ramThresholdSeconds);
  const auto [ow, oh] = computeOutputSize(cw, ch, d.outputHeight);

  ui::beginCard("estimate");
  ImGui::PushFont(f.bold);
  ImGui::TextUnformatted(tr("est.title"));
  ImGui::PopFont();
  ImGui::SameLine();
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(2));
  ui::mutedText(f, trf("est.hint", tracks, ow, oh), false);
  if (ImGui::BeginTable("##est", 3, ImGuiTableFlags_SizingStretchSame)) {
    auto cell = [&](const std::string& value, const std::string& label) {
      ImGui::TableNextColumn();
      ImGui::PushFont(f.title);
      ImGui::PushStyleColor(ImGuiCol_Text, theme::kText);
      ImGui::TextUnformatted(value.c_str());
      ImGui::PopStyleColor();
      ImGui::PopFont();
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(6));
      ui::mutedText(f, label);
    };
    cell(formatBytes(clip), trf("est.col.clip", durationLabel(d.replaySeconds)));
    cell(formatBytes(perMin), tr("est.col.rec"));
    if (buf.diskBytes == 0) cell(formatBytes(buf.ramBytes), tr("est.col.ram"));
    else cell(formatBytes(buf.diskBytes), trf("est.col.disk", formatBytes(buf.ramBytes)));
    ImGui::EndTable();
  }
  ui::endCard();
}

void SettingsPage::drawReplay(const UiFonts& f) {
  auto& d = form_.draft;
  ui::pageHeader(f, tr("sec.replay"), tr("sec.replay.desc"));
  ui::beginCard("replay");
  static const std::vector<int> durations{10, 15, 20, 30, 45, 60, 90, 120, 180, 240, 300, 600, 900, 1200};
  ui::beginRow(f, tr("rep.duration"));
  ui::stepSlider("##dur", durations, &d.replaySeconds, durationLabel(d.replaySeconds));
  ui::endRow();

  static const std::vector<int> thresholds{0, 30, 60, 120, 180, 300, 600, 1200};
  ui::beginRow(f, tr("rep.threshold"), tr("rep.threshold.hint"));
  ui::stepSlider("##thr", thresholds, &d.ramThresholdSeconds,
                 d.ramThresholdSeconds == 0 ? tr("rep.alwaysdisk") : durationLabel(d.ramThresholdSeconds));
  ui::endRow();

  const std::string folder = ctx_.bufferFolder ? pathToUtf8(ctx_.bufferFolder(d)) : d.tempDir;
  ui::beginRow(f, tr("rep.tempdir"), folder.c_str());
  ui::alignRight(d.tempDir.empty() ? px(96) : px(96) * 2 + px(10));
  if (!d.tempDir.empty()) {
    if (ui::secondaryButton(tr("common.default"), true, px(96))) d.tempDir.clear();
    ImGui::SameLine();
  }
  if (ui::secondaryButton(tr("common.change"), true, px(96)) && ctx_.pickFolder)
    if (const auto p = ctx_.pickFolder(ctx_.bufferFolder ? ctx_.bufferFolder(d) : std::filesystem::path{})) d.tempDir = pathToUtf8(*p);
  ui::endRow(false);
  ui::endCard();
  drawEstimate(f);
}

void SettingsPage::drawAudio(const UiFonts& f) {
  auto& d = form_.draft;
  ui::pageHeader(f, tr("sec.audio"), tr("sec.audio.desc"));
  ui::beginCard("audio");
  ui::beginRow(f, tr("aud.capturemic"));
  ui::toggle("##mic", &d.captureMic);
  ui::endRow();

  std::vector<std::string> micNames{tr("aud.mic.default")};
  int micIndex = 0;
  for (size_t i = 0; i < mics_.size(); ++i) {
    micNames.push_back(mics_[i].second);
    if (mics_[i].first == d.micDeviceId) micIndex = int(i) + 1;
  }
  ui::beginRow(f, tr("aud.mic"));
  ImGui::BeginDisabled(!d.captureMic);
  if (ui::combo("##micdev", micNames, &micIndex)) d.micDeviceId = micIndex == 0 ? std::string{} : mics_[size_t(micIndex - 1)].first;
  ImGui::EndDisabled();
  ui::endRow();

  ui::beginRow(f, tr("aud.mix"), tr("aud.mix.hint"));
  ui::toggle("##mix", &d.mixTrack);
  ui::endRow();

  static const std::vector<int> rates{96, 128, 160, 192};
  int ai = indexOf(rates, d.audioKbps, 2);
  ui::beginRow(f, tr("aud.bitrate"), tr("aud.bitrate.hint"));
  if (ui::segmented("##abr", {"96", "128", "160", "192"}, &ai)) d.audioKbps = rates[size_t(ai)];
  ui::endRow(false);
  ui::endCard();

  ui::beginCard("apps");
  const float startX = ImGui::GetCursorPosX();
  ui::cardTitle(f, tr("aud.apps"), tr("aud.apps.hint"));
  const float right = startX + ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight();
  const float afterTitle = ImGui::GetCursorPosY();
  ImGui::SetCursorPos({right, ImGui::GetStyle().WindowPadding.y});
  if (ui::iconButton(f, "##refresh", icon::kRefresh, tr("common.refresh"))) refreshLists();
  ImGui::SetCursorPos({startX, afterTitle});
  const auto rows = audioAppRows(apps_, d.audioBlacklist, ctx_.appName ? ctx_.appName : [](const std::string& e) { return e; });
  if (rows.empty()) ui::mutedText(f, tr("aud.apps.empty"));
  for (size_t i = 0; i < rows.size(); ++i) {
    ImGui::PushID(rows[i].exe.c_str());
    bool recorded = !rows[i].blacklisted;
    ui::beginRow(f, rows[i].name.c_str(), rows[i].exe.c_str());
    if (ui::toggle("##rec", &recorded)) toggleBlacklist(d.audioBlacklist, rows[i].exe, !recorded);
    ui::endRow(i + 1 < rows.size());
    ImGui::PopID();
  }
  ui::endCard();
}

void SettingsPage::stopCapture() {
  if (capturing_ >= 0 && ctx_.suspendHotkeys) ctx_.suspendHotkeys(false);
  capturing_ = -1;
}

void SettingsPage::hotkeyField(const UiFonts& f, int slot, std::string* value) {
  const float h = ImGui::GetFrameHeight();
  const float clearW = h;
  const float w = ui::controlWidth() - clearW - px(8);
  const bool capturing = capturing_ == slot;
  ImGui::PushID(slot);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const bool clicked = ImGui::InvisibleButton("##hk", {w, h});
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, {p.x + w, p.y + h}, capturing || hovered ? theme::kControlHover : theme::kControl, px(6));
  if (capturing) dl->AddRect(p, {p.x + w, p.y + h}, theme::accent(), px(6), 0, px(1.5f));
  if (capturing) {
    dl->AddText({p.x + px(12), p.y + (h - ImGui::GetTextLineHeight()) * 0.5f}, theme::kMuted, tr("hk.press"));
  } else if (value->empty()) {
    dl->AddText({p.x + px(12), p.y + (h - ImGui::GetTextLineHeight()) * 0.5f}, theme::kFaint, tr("hk.none"));
  } else {
    ui::drawKeycaps(f, *value, {p.x + px(6), p.y}, h);
  }
  ImGui::SameLine(0, px(8));
  if (ui::iconButton(f, "##clear", icon::kClose, tr("hk.clear"))) {
    value->clear();
    stopCapture();
  }
  ImGui::PopID();

  if (clicked && !capturing) {
    stopCapture();
    capturing_ = slot;
    hotkeyHint_.clear();
    if (ctx_.suspendHotkeys) ctx_.suspendHotkeys(true);
    return;
  }
  if (!capturing) return;
  const ImGuiIO& io = ImGui::GetIO();
  if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || (ImGui::IsMouseClicked(0) && !hovered)) {
    stopCapture();
    return;
  }
  for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
    const auto key = ImGuiKey(k);
    if (isModifier(key) || !ImGui::IsKeyPressed(key, false)) continue;
    const unsigned vk = vkFromImGuiKey(key);
    const auto text = vk ? hotkeyFromKeys(io.KeyCtrl, io.KeyAlt, io.KeyShift, io.KeySuper, vk) : std::nullopt;
    if (text) {
      *value = *text;
      hotkeyHint_.clear();
      stopCapture();
    } else {
      hotkeyHint_ = tr("hk.invalid");
    }
    break;
  }
}

void SettingsPage::drawHotkeys(const UiFonts& f) {
  auto& d = form_.draft;
  ui::pageHeader(f, tr("sec.hotkeys"), tr("sec.hotkeys.desc"));
  ui::beginCard("hotkeys");
  const std::pair<const char*, std::string*> rows[] = {
      {"hk.save", &d.hotkeys.saveClip}, {"hk.record", &d.hotkeys.toggleRecord}, {"hk.gallery", &d.hotkeys.openGallery}};
  for (int i = 0; i < 3; ++i) {
    ui::beginRow(f, tr(rows[i].first));
    hotkeyField(f, i, rows[i].second);
    ui::endRow(i < 2);
  }
  ui::endCard();
  if (!hotkeyHint_.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::kWarning);
    ImGui::TextUnformatted((std::string(icon::kWarning) + "  " + hotkeyHint_).c_str());
    ImGui::PopStyleColor();
  }
}

void SettingsPage::drawOutput(const UiFonts& f) {
  auto& d = form_.draft;
  ui::pageHeader(f, tr("sec.output"), tr("sec.output.desc"));
  ui::beginCard("output");
  const auto folder = ctx_.clipsFolder ? ctx_.clipsFolder(d) : std::filesystem::path(utf8ToWide(d.outputDir));
  const std::string folderText = pathToUtf8(folder);
  ui::beginRow(f, tr("out.folder"), folderText.c_str());
  ui::alignRight(px(96) * 2 + px(10));
  if (ui::secondaryButton(tr("common.open"), true, px(96)) && ctx_.openFolder) ctx_.openFolder(folder);
  ImGui::SameLine();
  if (ui::secondaryButton(tr("common.change"), true, px(96)) && ctx_.pickFolder)
    if (const auto p = ctx_.pickFolder(folder)) d.outputDir = pathToUtf8(*p);
  ui::endRow();

  int ci = d.container == "mkv" ? 1 : 0;
  ui::beginRow(f, tr("out.container"), tr("out.container.hint"));
  if (ui::segmented("##container", {"MP4", "MKV"}, &ci)) d.container = ci == 1 ? "mkv" : "mp4";
  ui::endRow();

  ui::beginRow(f, tr("out.name"), tr("out.name.hint"));
  ImGui::SetNextItemWidth(ui::controlWidth());
  ImGui::InputText("##name", &d.nameTemplate);
  const std::string example =
      sanitizeFileName(expandNameTemplate(d.nameTemplate, "Valorant", std::chrono::system_clock::now())) + "." + d.container;
  ui::mutedText(f, trf("out.preview", example), false);
  ui::endRow(false);
  ui::endCard();
}

void SettingsPage::drawGeneral(const UiFonts& f) {
  auto& d = form_.draft;
  ui::pageHeader(f, tr("sec.general"), tr("sec.general.desc"));
  ui::beginCard("general");
  ui::beginRow(f, tr("gen.autostart"));
  ui::toggle("##autostart", &autostartDraft_);
  ui::endRow();
  ui::beginRow(f, tr("gen.sounds"));
  ui::toggle("##sounds", &d.sounds);
  ui::endRow();

  static const std::vector<std::string> corners{"top-left", "top-right", "bottom-left", "bottom-right"};
  int ki = indexOf(corners, d.toastCorner, 1);
  ui::beginRow(f, tr("gen.toast"));
  if (ui::combo("##corner", {tr("corner.tl"), tr("corner.tr"), tr("corner.bl"), tr("corner.br")}, &ki))
    d.toastCorner = corners[size_t(ki)];
  ui::endRow();

  int li = d.language == "en" ? 1 : 0;
  ui::beginRow(f, tr("gen.language"));
  if (ui::segmented("##lang", {"Italiano", "English"}, &li)) d.language = li == 1 ? "en" : "it";
  ui::endRow();

  ui::beginRow(f, tr("gen.accent"));
  const float sw = px(28), gap = px(8);
  ui::alignRight(sw * float(std::size(kAccents)) + gap * float(std::size(kAccents) - 1));
  for (size_t i = 0; i < std::size(kAccents); ++i) {
    if (i) ImGui::SameLine(0, gap);
    ImGui::PushID(int(i));
    const bool selected = toLowerAscii(d.accent) == toLowerAscii(kAccents[i]);
    if (ui::colorSwatch("##sw", *parseHexColor(kAccents[i]), selected)) d.accent = kAccents[i];
    ImGui::PopID();
  }
  ui::endRow(false);
  ui::endCard();
}

void SettingsPage::drawFooter(const UiFonts& f, float height) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::kPanel);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {px(40), 0});
  ImGui::BeginChild("##footer", {0, height}, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 wp = ImGui::GetWindowPos();
  dl->AddLine(wp, {wp.x + ImGui::GetWindowWidth(), wp.y}, theme::kBorder);

  const float btnH = ImGui::GetFrameHeight() + px(2);
  const float y = (height - btnH) * 0.5f;
  const bool isDirty = dirty();
  if (isDirty) message_.clear();
  ImGui::SetCursorPos({px(40), (height - ImGui::GetTextLineHeight()) * 0.5f});
  if (!error_.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::kDanger);
    ImGui::TextUnformatted((std::string(icon::kWarning) + "  " + error_).c_str());
    ImGui::PopStyleColor();
  } else if (isDirty) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::kWarning);
    ImGui::TextUnformatted((std::string(icon::kInfo) + "  " + tr("footer.unsaved")).c_str());
    ImGui::PopStyleColor();
  } else if (!message_.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::accent());
    ImGui::TextUnformatted((std::string(icon::kCheck) + "  " + message_).c_str());
    ImGui::PopStyleColor();
  }
  ImGui::PushFont(f.bold);
  const float applyW = ImGui::CalcTextSize(tr("btn.apply")).x + px(40);
  ImGui::PopFont();
  const float revertW = ImGui::CalcTextSize(tr("btn.revert")).x + px(32);
  ImGui::SetCursorPos({ImGui::GetWindowWidth() - px(40) - applyW - revertW - px(10), y});
  if (ui::secondaryButton(tr("btn.revert"), isDirty, revertW)) {
    form_.revert();
    autostartDraft_ = autostartSaved_;
    error_.clear();
    stopCapture();
  }
  ImGui::SameLine(0, px(10));
  ImGui::PushFont(f.bold);
  if (ui::primaryButton(tr("btn.apply"), isDirty, applyW)) applyDraft();
  ImGui::PopFont();
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
}

}  // namespace clipper
