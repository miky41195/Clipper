#include "platform/Toast.h"
#include <shellscalingapi.h>

namespace clipper {
namespace {
constexpr UINT_PTR kHoldTimer = 1;
constexpr UINT_PTR kFadeTimer = 2;
constexpr int kFadeSteps = 10;
const wchar_t* kClass = L"ClipperToast";

D2D1_COLOR_F rgb(int r, int g, int b, float a = 1.f) { return {r / 255.f, g / 255.f, b / 255.f, a}; }
}  // namespace

Toast::Toast() {
  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = kClass;
  RegisterClassExW(&wc);
  hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kClass,
                          L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
  SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  winrt::check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.put()));
  winrt::check_hresult(
      DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwrite_.put())));
}

Toast::~Toast() {
  releaseSurface();
  if (hwnd_) DestroyWindow(hwnd_);
}

void Toast::releaseSurface() {
  if (memDC_) {
    SelectObject(memDC_, oldBitmap_);
    DeleteDC(memDC_);
    memDC_ = nullptr;
  }
  if (bitmap_) {
    DeleteObject(bitmap_);
    bitmap_ = nullptr;
  }
}

void Toast::show(const std::wstring& title, const std::wstring& body, std::string_view corner, ToastKind kind) {
  KillTimer(hwnd_, kHoldTimer);
  KillTimer(hwnd_, kFadeTimer);
  releaseSurface();

  HMONITOR mon = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO mi{sizeof(mi)};
  GetMonitorInfoW(mon, &mi);
  UINT dpiX = 96, dpiY = 96;
  GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
  const float scale = dpiX / 96.f;
  size_.cx = LONG(340 * scale);
  size_.cy = LONG(72 * scale);
  const LONG margin = LONG(20 * scale);
  const RECT& wa = mi.rcWork;
  const bool left = corner.find("left") != std::string_view::npos;
  const bool bottom = corner.find("bottom") != std::string_view::npos;
  pos_.x = left ? wa.left + margin : wa.right - margin - size_.cx;
  pos_.y = bottom ? wa.bottom - margin - size_.cy : wa.top + margin;

  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = size_.cx;
  bi.bmiHeader.biHeight = -size_.cy;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  HDC screen = GetDC(nullptr);
  memDC_ = CreateCompatibleDC(screen);
  void* bits = nullptr;
  bitmap_ = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  ReleaseDC(nullptr, screen);
  oldBitmap_ = SelectObject(memDC_, bitmap_);

  const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                                  D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  winrt::com_ptr<ID2D1DCRenderTarget> rt;
  if (FAILED(d2d_->CreateDCRenderTarget(&props, rt.put()))) return;
  const RECT rc{0, 0, size_.cx, size_.cy};
  rt->BindDC(memDC_, &rc);
  rt->SetDpi(96.f * scale, 96.f * scale);
  rt->BeginDraw();
  rt->Clear(rgb(0, 0, 0, 0));
  const float w = 340.f, h = 72.f;
  winrt::com_ptr<ID2D1SolidColorBrush> bg, border, accent, titleBrush, bodyBrush;
  rt->CreateSolidColorBrush(rgb(22, 23, 28, 0.94f), bg.put());
  rt->CreateSolidColorBrush(rgb(255, 255, 255, 0.08f), border.put());
  const D2D1_COLOR_F accentColor = kind == ToastKind::Recording ? rgb(255, 69, 58)
                                   : kind == ToastKind::Error   ? rgb(255, 159, 10)
                                                                : rgb(61, 220, 132);
  rt->CreateSolidColorBrush(accentColor, accent.put());
  rt->CreateSolidColorBrush(rgb(245, 245, 247), titleBrush.put());
  rt->CreateSolidColorBrush(rgb(160, 163, 172), bodyBrush.put());
  const D2D1_ROUNDED_RECT card{{0.5f, 0.5f, w - 0.5f, h - 0.5f}, 12.f, 12.f};
  rt->FillRoundedRectangle(card, bg.get());
  rt->DrawRoundedRectangle(card, border.get(), 1.f);
  rt->FillEllipse({{24.f, h / 2}, 6.f, 6.f}, accent.get());

  winrt::com_ptr<IDWriteTextFormat> titleFmt, bodyFmt;
  dwrite_->CreateTextFormat(L"Segoe UI Variable Display", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                            DWRITE_FONT_STRETCH_NORMAL, 15.f, L"it-it", titleFmt.put());
  dwrite_->CreateTextFormat(L"Segoe UI Variable Text", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
                            DWRITE_FONT_STRETCH_NORMAL, 12.5f, L"it-it", bodyFmt.put());
  for (auto* f : {titleFmt.get(), bodyFmt.get()}) {
    f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    f->SetTrimming(&trim, nullptr);
  }
  rt->DrawTextW(title.c_str(), UINT32(title.size()), titleFmt.get(), {44.f, 14.f, w - 16.f, 36.f}, titleBrush.get());
  rt->DrawTextW(body.c_str(), UINT32(body.size()), bodyFmt.get(), {44.f, 38.f, w - 16.f, 60.f}, bodyBrush.get());
  rt->EndDraw();

  present(255);
  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  fadeStep_ = 0;
  SetTimer(hwnd_, kHoldTimer, 2600, nullptr);
}

void Toast::present(BYTE alpha) {
  if (!memDC_) return;
  POINT src{0, 0};
  BLENDFUNCTION blend{AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
  HDC screen = GetDC(nullptr);
  UpdateLayeredWindow(hwnd_, screen, &pos_, &size_, memDC_, &src, 0, &blend, ULW_ALPHA);
  ReleaseDC(nullptr, screen);
}

LRESULT CALLBACK Toast::proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  auto* self = reinterpret_cast<Toast*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_TIMER && self) {
    if (wp == kHoldTimer) {
      KillTimer(hwnd, kHoldTimer);
      SetTimer(hwnd, kFadeTimer, 25, nullptr);
    } else if (wp == kFadeTimer) {
      ++self->fadeStep_;
      if (self->fadeStep_ >= kFadeSteps) {
        KillTimer(hwnd, kFadeTimer);
        ShowWindow(hwnd, SW_HIDE);
        self->releaseSurface();
      } else {
        self->present(BYTE(255 * (kFadeSteps - self->fadeStep_) / kFadeSteps));
      }
    }
    return 0;
  }
  if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace clipper
