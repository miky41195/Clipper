#pragma once
#include "gpu/GpuDevice.h"
#include "ui/Fonts.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <filesystem>
#include <functional>
#include <winrt/base.h>

struct ImGuiContext;

namespace clipper {

using DrawFn = std::function<void(const UiFonts&)>;

// On-demand app window: Win32 + its own D3D11 swap chain + ImGui context. Everything is created in
// show() and released when the window closes, so a closed UI costs nothing.
class UiWindow {
public:
  UiWindow(HINSTANCE inst, DrawFn draw, std::function<void()> onClosed = {});
  ~UiWindow();
  UiWindow(const UiWindow&) = delete;
  UiWindow& operator=(const UiWindow&) = delete;

  void show();
  void close();
  bool open() const { return hwnd_ != nullptr; }
  bool needsFrames() const;
  void requestFrames(int n = 3);
  void setAnimating(bool on) { animating_ = on; }
  void setAccent(ImU32 accent);
  void renderFrame();
  HWND hwnd() const { return hwnd_; }
  std::function<void()> onClosing;  // called before the device is released
  std::function<GpuDevice()> deviceProvider;  // optional shared device (else the window creates its own)

private:
  static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
  LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
  void create();
  void destroy();
  void createTarget();
  void dumpIfRequested();
  int frames_ = 0;

  HINSTANCE inst_;
  DrawFn draw_;
  std::function<void()> onClosed_;
  HWND hwnd_ = nullptr;
  winrt::com_ptr<ID3D11Device> device_;
  winrt::com_ptr<ID3D11DeviceContext> context_;
  winrt::com_ptr<IDXGISwapChain1> swap_;
  winrt::com_ptr<ID3D11RenderTargetView> rtv_;
  ImGuiContext* ctx_ = nullptr;
  UiFonts fonts_;
  ImU32 accent_ = IM_COL32(61, 220, 132, 255);
  float scale_ = 1.f;
  int settle_ = 0;
  bool animating_ = false;
  bool rebuildFonts_ = false;
  bool closeRequested_ = false;
};

// Draws the root full-window container and calls draw inside it.
void drawRoot(const UiFonts& fonts, const DrawFn& draw);

// Renders `draw` into a w x h image (at the given DPI scale) and saves it as PNG. Used for visual checks.
bool renderOffscreen(int w, int h, float scale, ImU32 accent, const DrawFn& draw, const std::filesystem::path& png,
                     ImVec2 mouse = {-1, -1});

}  // namespace clipper
