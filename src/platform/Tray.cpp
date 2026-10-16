#include "platform/Tray.h"
#include "core/Text.h"
#include <shellapi.h>
#include <algorithm>
#include <cmath>

namespace clipper {
namespace {

// Anti-aliased colored dot with a soft dark ring, drawn at runtime (no icon assets).
HICON makeDotIcon(COLORREF color, int size = 32) {
  BITMAPV5HEADER bi{};
  bi.bV5Size = sizeof(bi);
  bi.bV5Width = size;
  bi.bV5Height = -size;
  bi.bV5Planes = 1;
  bi.bV5BitCount = 32;
  bi.bV5Compression = BI_BITFIELDS;
  bi.bV5RedMask = 0x00FF0000;
  bi.bV5GreenMask = 0x0000FF00;
  bi.bV5BlueMask = 0x000000FF;
  bi.bV5AlphaMask = 0xFF000000;
  HDC dc = GetDC(nullptr);
  void* bits = nullptr;
  HBITMAP color32 = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
  ReleaseDC(nullptr, dc);
  auto* px = static_cast<uint32_t*>(bits);
  const double c = (size - 1) / 2.0, outer = size * 0.46, inner = size * 0.36;
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x) {
      const double d = std::hypot(x - c, y - c);
      const double aOuter = std::clamp(outer - d + 0.5, 0.0, 1.0);
      const double aInner = std::clamp(inner - d + 0.5, 0.0, 1.0);
      // ring: near-black; core: the state color
      const double r = GetRValue(color) * aInner + 20 * (1 - aInner);
      const double g = GetGValue(color) * aInner + 20 * (1 - aInner);
      const double b = GetBValue(color) * aInner + 24 * (1 - aInner);
      const double a = aOuter;
      px[y * size + x] = (uint32_t(a * 255) << 24) | (uint32_t(r * a) << 16) | (uint32_t(g * a) << 8) | uint32_t(b * a);
    }
  HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
  ICONINFO ii{TRUE, 0, 0, mask, color32};
  HICON icon = CreateIconIndirect(&ii);
  DeleteObject(mask);
  DeleteObject(color32);
  return icon;
}

}  // namespace

TrayIcon::TrayIcon(HWND hwnd) : hwnd_(hwnd) {
  icons_[int(TrayState::Buffering)] = makeDotIcon(RGB(61, 220, 132));
  icons_[int(TrayState::Recording)] = makeDotIcon(RGB(255, 69, 58));
  icons_[int(TrayState::Paused)] = makeDotIcon(RGB(142, 142, 147));
  icons_[int(TrayState::Error)] = makeDotIcon(RGB(255, 159, 10));
  add();
}

TrayIcon::~TrayIcon() {
  NOTIFYICONDATAW nid{sizeof(nid)};
  nid.hWnd = hwnd_;
  nid.uID = 1;
  Shell_NotifyIconW(NIM_DELETE, &nid);
  for (HICON i : icons_) DestroyIcon(i);
}

void TrayIcon::add() {
  NOTIFYICONDATAW nid{sizeof(nid)};
  nid.hWnd = hwnd_;
  nid.uID = 1;
  nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  nid.uCallbackMessage = kCallbackMsg;
  nid.hIcon = icons_[int(state_)];
  wcsncpy_s(nid.szTip, tip_.c_str(), _TRUNCATE);
  Shell_NotifyIconW(NIM_ADD, &nid);
  nid.uVersion = NOTIFYICON_VERSION_4;
  Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void TrayIcon::recreate() { add(); }

void TrayIcon::setState(TrayState s, const std::wstring& tooltip) {
  state_ = s;
  tip_ = tooltip;
  NOTIFYICONDATAW nid{sizeof(nid)};
  nid.hWnd = hwnd_;
  nid.uID = 1;
  nid.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  nid.hIcon = icons_[int(s)];
  wcsncpy_s(nid.szTip, tip_.c_str(), _TRUNCATE);
  Shell_NotifyIconW(NIM_MODIFY, &nid);
}

UINT TrayIcon::showMenu(bool recording, bool paused, const HotkeySettings& hk) {
  auto label = [](const wchar_t* text, const std::string& key) {
    return key.empty() ? std::wstring(text) : std::wstring(text) + L"\t" + utf8ToWide(key);
  };
  HMENU m = CreatePopupMenu();
  AppendMenuW(m, MF_STRING | (paused ? MF_GRAYED : 0), CmdSave, label(L"Salva clip", hk.saveClip).c_str());
  AppendMenuW(m, MF_STRING | (paused ? MF_GRAYED : 0), CmdRecord,
              label(recording ? L"Ferma registrazione" : L"Avvia registrazione", hk.toggleRecord).c_str());
  AppendMenuW(m, MF_STRING | (recording ? MF_GRAYED : 0), CmdPause, paused ? L"Riprendi cattura" : L"Metti in pausa");
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING, CmdGallery, label(L"Galleria", hk.openGallery).c_str());
  AppendMenuW(m, MF_STRING, CmdSettings, L"Impostazioni");
  AppendMenuW(m, MF_STRING, CmdOpenFolder, L"Apri cartella clip");
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING, CmdQuit, L"Esci");
  SetMenuDefaultItem(m, CmdSave, FALSE);
  POINT pt;
  GetCursorPos(&pt);
  SetForegroundWindow(hwnd_);
  const UINT cmd = UINT(TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd_, nullptr));
  PostMessageW(hwnd_, WM_NULL, 0, 0);
  DestroyMenu(m);
  return cmd;
}

}  // namespace clipper
