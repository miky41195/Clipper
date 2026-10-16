#pragma once
#include "ui/UiContext.h"
#include <filesystem>
#include <string>

namespace clipper {

// Fake data so pages can be rendered offscreen (tests, `clipper.exe --render-ui`).
UiContext previewContext();

// page: "gallery", "settings.capture", "settings.replay", "settings.audio", "settings.hotkeys",
// "settings.output", "settings.general". Optional "@x,y" suffix places the mouse (hover states).
class AppUi;
bool renderPreview(const std::string& page, const std::filesystem::path& png, float scale, UiContext* ctx = nullptr,
                   const std::function<void(AppUi&)>& setup = {});

}  // namespace clipper
