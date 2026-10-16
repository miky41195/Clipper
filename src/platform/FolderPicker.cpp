#include "platform/FolderPicker.h"
#include <shobjidl.h>
#include <winrt/base.h>

namespace clipper {

std::optional<std::filesystem::path> pickFolder(HWND owner, const std::filesystem::path& initial) {
  try {
    auto dlg = winrt::create_instance<IFileOpenDialog>(CLSID_FileOpenDialog);
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    if (!initial.empty()) {
      winrt::com_ptr<IShellItem> folder;
      if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(folder.put())))) dlg->SetFolder(folder.get());
    }
    if (dlg->Show(owner) != S_OK) return std::nullopt;
    winrt::com_ptr<IShellItem> item;
    winrt::check_hresult(dlg->GetResult(item.put()));
    PWSTR path = nullptr;
    winrt::check_hresult(item->GetDisplayName(SIGDN_FILESYSPATH, &path));
    std::filesystem::path result(path);
    CoTaskMemFree(path);
    return result;
  } catch (...) {
    return std::nullopt;
  }
}

}  // namespace clipper
