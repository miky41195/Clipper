#include "gallery/FileOps.h"
#include "app/ClipNaming.h"
#include "core/Text.h"
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <cstring>
#include <winrt/base.h>

namespace clipper {
namespace fs = std::filesystem;

fs::path renameClip(const fs::path& file, const std::string& newStem) {
  const std::string stem = sanitizeFileName(newStem);
  if (stem == pathToUtf8(file.stem())) return file;
  const fs::path target = uniquePath(file.parent_path(), stem, pathToUtf8(file.extension()));
  fs::rename(file, target);
  return target;
}

bool recycle(const fs::path& file) {
  if (!fs::exists(file)) return false;
  try {
    auto op = winrt::create_instance<IFileOperation>(CLSID_FileOperation);
    winrt::check_hresult(op->SetOperationFlags(FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI | FOFX_RECYCLEONDELETE));
    winrt::com_ptr<IShellItem> item;
    winrt::check_hresult(SHCreateItemFromParsingName(file.c_str(), nullptr, IID_PPV_ARGS(item.put())));
    winrt::check_hresult(op->DeleteItem(item.get(), nullptr));
    winrt::check_hresult(op->PerformOperations());
  } catch (...) {
    return false;
  }
  return !fs::exists(file);
}

void revealInExplorer(const fs::path& file) {
  PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(file.c_str());
  if (!pidl) return;
  SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
  ILFree(pidl);
}

bool copyFileToClipboard(const fs::path& file, HWND owner) {
  const std::wstring path = file.wstring();
  const size_t bytes = sizeof(DROPFILES) + (path.size() + 2) * sizeof(wchar_t);
  HGLOBAL mem = GlobalAlloc(GHND, bytes);
  if (!mem) return false;
  auto* df = static_cast<DROPFILES*>(GlobalLock(mem));
  df->pFiles = sizeof(DROPFILES);
  df->fWide = TRUE;
  std::memcpy(reinterpret_cast<BYTE*>(df) + sizeof(DROPFILES), path.c_str(), path.size() * sizeof(wchar_t));
  GlobalUnlock(mem);  // GHND zero-fills: the list ends with the required double null
  if (!OpenClipboard(owner)) {
    GlobalFree(mem);
    return false;
  }
  EmptyClipboard();
  const bool ok = SetClipboardData(CF_HDROP, mem) != nullptr;
  if (!ok) GlobalFree(mem);
  if (HGLOBAL effect = GlobalAlloc(GHND, sizeof(DWORD))) {
    *static_cast<DWORD*>(GlobalLock(effect)) = DROPEFFECT_COPY;
    GlobalUnlock(effect);
    if (!SetClipboardData(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT), effect)) GlobalFree(effect);
  }
  CloseClipboard();
  return ok;
}

}  // namespace clipper
