#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <string_view>
#include <winrt/base.h>

namespace clipper {

enum class ToastKind { Info, Recording, Error };

// Small click-through overlay in a screen corner. Never takes focus, so it is safe over games.
// Must be used from the thread that runs the message loop.
class Toast {
public:
  Toast();
  ~Toast();
  Toast(const Toast&) = delete;
  Toast& operator=(const Toast&) = delete;

  void show(const std::wstring& title, const std::wstring& body, std::string_view corner, ToastKind kind = ToastKind::Info);

private:
  static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
  void present(BYTE alpha);
  void releaseSurface();

  HWND hwnd_ = nullptr;
  winrt::com_ptr<ID2D1Factory> d2d_;
  winrt::com_ptr<IDWriteFactory> dwrite_;
  HDC memDC_ = nullptr;
  HBITMAP bitmap_ = nullptr;
  HGDIOBJ oldBitmap_ = nullptr;
  POINT pos_{};
  SIZE size_{};
  int fadeStep_ = 0;
};

}  // namespace clipper
