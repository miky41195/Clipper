#include "ui/UiWindow.h"
#include "ui/ImageIO.h"
#include "ui/Texture.h"
#include "ui/Theme.h"
#include <dwmapi.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace clipper {
using winrt::check_hresult;
using winrt::com_ptr;

namespace {
const wchar_t* kClass = L"ClipperWindow";
constexpr float kClear[4] = {15 / 255.f, 17 / 255.f, 21 / 255.f, 1.f};

void createDevice(com_ptr<ID3D11Device>& dev, com_ptr<ID3D11DeviceContext>& ctx) {
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
  check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 1,
                                  D3D11_SDK_VERSION, dev.put(), nullptr, ctx.put()));
}
}  // namespace

void drawRoot(const UiFonts& fonts, const DrawFn& draw) {
  const ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::kBg);
  ImGui::Begin("##root", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleColor();
  ImGui::PushFont(fonts.body);
  draw(fonts);
  ImGui::PopFont();
  ImGui::End();
}

UiWindow::UiWindow(HINSTANCE inst, DrawFn draw, std::function<void()> onClosed)
    : inst_(inst), draw_(std::move(draw)), onClosed_(std::move(onClosed)) {}

UiWindow::~UiWindow() { destroy(); }

void UiWindow::setAccent(ImU32 accent) {
  accent_ = accent;
  if (ctx_) {
    ImGui::SetCurrentContext(ctx_);
    theme::apply(accent_, scale_);
    requestFrames();
  }
}

bool UiWindow::needsFrames() const { return hwnd_ && (settle_ > 0 || animating_ || closeRequested_); }

void UiWindow::requestFrames(int n) {
  if (n > settle_) settle_ = n;
}

void UiWindow::show() {
  if (!hwnd_) create();
  if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
  ShowWindow(hwnd_, SW_SHOW);
  SetForegroundWindow(hwnd_);
  requestFrames(4);
}

void UiWindow::close() {
  if (!hwnd_) return;
  ShowWindow(hwnd_, SW_HIDE);
  closeRequested_ = true;
}

void UiWindow::create() {
  WNDCLASSEXW wc{sizeof(wc)};
  wc.style = CS_CLASSDC;
  wc.lpfnWndProc = proc;
  wc.hInstance = inst_;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
  wc.hbrBackground = CreateSolidBrush(RGB(15, 17, 21));
  wc.lpszClassName = kClass;
  RegisterClassExW(&wc);

  POINT cursor;
  GetCursorPos(&cursor);
  HMONITOR mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO mi{sizeof(mi)};
  GetMonitorInfoW(mon, &mi);
  UINT dpi = GetDpiForSystem();
  scale_ = dpi / 96.f;
  const int w = int(1120 * scale_), h = int(740 * scale_);
  const RECT& wa = mi.rcWork;
  const int x = wa.left + (wa.right - wa.left - w) / 2, y = wa.top + (wa.bottom - wa.top - h) / 2;
  hwnd_ = CreateWindowExW(0, kClass, L"Clipper", WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr, nullptr, inst_, this);
  const UINT realDpi = GetDpiForWindow(hwnd_);
  if (realDpi && realDpi != dpi) scale_ = realDpi / 96.f;

  const BOOL dark = TRUE;
  DwmSetWindowAttribute(hwnd_, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
  const int corner = 2 /*DWMWCP_ROUND*/;
  DwmSetWindowAttribute(hwnd_, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &corner, sizeof(corner));
  const COLORREF caption = RGB(15, 17, 21);
  DwmSetWindowAttribute(hwnd_, 35 /*DWMWA_CAPTION_COLOR*/, &caption, sizeof(caption));

  createDevice(device_, context_);
  com_ptr<IDXGIFactory2> factory;
  com_ptr<IDXGIAdapter> adapter;
  device_.as<IDXGIDevice>()->GetAdapter(adapter.put());
  check_hresult(adapter->GetParent(IID_PPV_ARGS(factory.put())));
  DXGI_SWAP_CHAIN_DESC1 sd{};
  sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  sd.SampleDesc.Count = 1;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.BufferCount = 2;
  sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  check_hresult(factory->CreateSwapChainForHwnd(device_.get(), hwnd_, &sd, nullptr, nullptr, swap_.put()));
  factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
  createTarget();

  IMGUI_CHECKVERSION();
  ctx_ = ImGui::CreateContext();
  ImGui::SetCurrentContext(ctx_);
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigInputTextCursorBlink = false;  // no idle redraws for a blinking caret
  ImGui_ImplWin32_Init(hwnd_);
  ImGui_ImplDX11_Init(device_.get(), context_.get());
  setUiDevice(device_.get());
  fonts_ = loadFonts(scale_);
  theme::apply(accent_, scale_);
  closeRequested_ = false;
}

void UiWindow::createTarget() {
  com_ptr<ID3D11Texture2D> back;
  check_hresult(swap_->GetBuffer(0, IID_PPV_ARGS(back.put())));
  rtv_ = nullptr;
  check_hresult(device_->CreateRenderTargetView(back.get(), nullptr, rtv_.put()));
}

void UiWindow::destroy() {
  if (hwnd_ && onClosing) onClosing();  // let pages drop textures and players first
  if (!hwnd_) return;
  if (ctx_) {
    ImGui::SetCurrentContext(ctx_);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(ctx_);
    ctx_ = nullptr;
  }
  if (context_) {
    // D3D11 defers destruction until the pipeline lets go of objects: unbind everything and flush,
    // otherwise the driver keeps the swap chain / textures alive after the window is gone.
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    context_->ClearState();
    context_->Flush();
  }
  rtv_ = nullptr;
  swap_ = nullptr;
  if (context_) {
    context_->ClearState();
    context_->Flush();
  }
  context_ = nullptr;
  device_ = nullptr;
  setUiDevice(nullptr);
  HWND h = hwnd_;
  hwnd_ = nullptr;
  SetWindowLongPtrW(h, GWLP_USERDATA, 0);
  DestroyWindow(h);
  settle_ = 0;
  animating_ = false;
  closeRequested_ = false;
  if (onClosed_) onClosed_();
}

void UiWindow::renderFrame() {
  if (closeRequested_) {
    destroy();
    return;
  }
  if (!hwnd_) return;
  if (IsIconic(hwnd_)) {
    settle_ = 0;
    return;
  }
  ImGui::SetCurrentContext(ctx_);
  if (rebuildFonts_) {
    ImGui_ImplDX11_InvalidateDeviceObjects();
    fonts_ = loadFonts(scale_);
    theme::apply(accent_, scale_);
    rebuildFonts_ = false;
  }
  ImGui_ImplDX11_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();
  drawRoot(fonts_, draw_);
  ImGui::Render();
  ID3D11RenderTargetView* rtv = rtv_.get();
  context_->OMSetRenderTargets(1, &rtv, nullptr);
  context_->ClearRenderTargetView(rtv, kClear);
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
  dumpIfRequested();
  swap_->Present(1, 0);
  if (settle_ > 0) --settle_;
}

// Debug aid: CLIPPER_UI_DUMP=<png> saves the window's own back buffer after a few frames.
void UiWindow::dumpIfRequested() {
  if (++frames_ != 3) return;
  wchar_t path[MAX_PATH]{};
  if (!GetEnvironmentVariableW(L"CLIPPER_UI_DUMP", path, MAX_PATH)) return;
  com_ptr<ID3D11Texture2D> back;
  if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(back.put())))) return;
  D3D11_TEXTURE2D_DESC td{};
  back->GetDesc(&td);
  td.Usage = D3D11_USAGE_STAGING;
  td.BindFlags = 0;
  td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  td.MiscFlags = 0;
  com_ptr<ID3D11Texture2D> staging;
  if (FAILED(device_->CreateTexture2D(&td, nullptr, staging.put()))) return;
  context_->CopyResource(staging.get(), back.get());
  D3D11_MAPPED_SUBRESOURCE m{};
  if (FAILED(context_->Map(staging.get(), 0, D3D11_MAP_READ, 0, &m))) return;
  writeImage(path, static_cast<const uint8_t*>(m.pData), int(td.Width), int(td.Height), int(m.RowPitch), ImageFormat::Png);
  context_->Unmap(staging.get(), 0);
}

LRESULT CALLBACK UiWindow::proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
  }
  auto* self = reinterpret_cast<UiWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self && self->ctx_) return self->handle(msg, wp, lp);
  return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT UiWindow::handle(UINT msg, WPARAM wp, LPARAM lp) {
  ImGui::SetCurrentContext(ctx_);
  requestFrames(3);
  if (ImGui_ImplWin32_WndProcHandler(hwnd_, msg, wp, lp)) return 1;
  switch (msg) {
    case WM_SIZE:
      if (swap_ && wp != SIZE_MINIMIZED) {
        rtv_ = nullptr;
        swap_->ResizeBuffers(0, LOWORD(lp), HIWORD(lp), DXGI_FORMAT_UNKNOWN, 0);
        createTarget();
        renderFrame();  // keep content live while resizing
      }
      return 0;
    case WM_GETMINMAXINFO: {
      auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
      mm->ptMinTrackSize = {LONG(820 * scale_), LONG(560 * scale_)};
      return 0;
    }
    case WM_DPICHANGED: {
      scale_ = HIWORD(wp) / 96.f;
      rebuildFonts_ = true;
      const RECT* r = reinterpret_cast<RECT*>(lp);
      SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_CLOSE:
      close();
      return 0;
    case WM_ERASEBKGND:
      return 1;
  }
  return DefWindowProcW(hwnd_, msg, wp, lp);
}

bool renderOffscreen(int w, int h, float scale, ImU32 accent, const DrawFn& draw, const std::filesystem::path& png,
                     ImVec2 mouse) {
  try {
    com_ptr<ID3D11Device> dev;
    com_ptr<ID3D11DeviceContext> ctx;
    createDevice(dev, ctx);
    D3D11_TEXTURE2D_DESC td{};
    td.Width = UINT(w);
    td.Height = UINT(h);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    com_ptr<ID3D11Texture2D> target;
    check_hresult(dev->CreateTexture2D(&td, nullptr, target.put()));
    com_ptr<ID3D11RenderTargetView> rtv;
    check_hresult(dev->CreateRenderTargetView(target.get(), nullptr, rtv.put()));
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    com_ptr<ID3D11Texture2D> staging;
    check_hresult(dev->CreateTexture2D(&td, nullptr, staging.put()));

    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGuiContext* ic = ImGui::CreateContext();
    ImGui::SetCurrentContext(ic);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui_ImplDX11_Init(dev.get(), ctx.get());
    ID3D11Device* prevDevice = uiDevice();
    setUiDevice(dev.get());
    const UiFonts fonts = loadFonts(scale);
    theme::apply(accent, scale);
    for (int i = 0; i < 4; ++i) {  // a few frames so auto-sized layouts settle
      io.DisplaySize = {float(w), float(h)};
      io.DeltaTime = 1.f / 60.f;
      ImGui_ImplDX11_NewFrame();
      io.AddMousePosEvent(mouse.x, mouse.y);
      ImGui::NewFrame();
      drawRoot(fonts, draw);
      ImGui::Render();
      ID3D11RenderTargetView* r = rtv.get();
      ctx->OMSetRenderTargets(1, &r, nullptr);
      ctx->ClearRenderTargetView(r, kClear);
      ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
    ctx->CopyResource(staging.get(), target.get());
    D3D11_MAPPED_SUBRESOURCE m{};
    check_hresult(ctx->Map(staging.get(), 0, D3D11_MAP_READ, 0, &m));
    const bool ok = writeImage(png, static_cast<const uint8_t*>(m.pData), w, h, int(m.RowPitch), ImageFormat::Png);
    ctx->Unmap(staging.get(), 0);
    ImGui_ImplDX11_Shutdown();
    ImGui::DestroyContext(ic);
    setUiDevice(prevDevice);
    ImGui::SetCurrentContext(prev);
    return ok;
  } catch (...) {
    return false;
  }
}

}  // namespace clipper
