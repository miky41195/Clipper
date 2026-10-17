#include "platform/FolderPicker.h"
#include <shobjidl.h>
#include <thread>
#include <winrt/base.h>

namespace clipper {
namespace {

std::optional<std::filesystem::path> showDialog(HWND owner, const std::filesystem::path& initial) {
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

}  // namespace

std::optional<std::filesystem::path> pickFolder(HWND owner, const std::filesystem::path& initial) {
  // Shell dialogs need a single-threaded apartment, but Clipper's UI thread is MTA: calling Show() there
  // hangs. Run the dialog on its own STA thread and keep this thread pumping messages meanwhile, because
  // the dialog talks to its owner window, which lives here.
  std::optional<std::filesystem::path> result;
  std::thread dialog([&] {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    result = showDialog(owner, initial);
    CoUninitialize();
  });
  HANDLE h = dialog.native_handle();
  bool quit = false;
  int quitCode = 0;
  while (MsgWaitForMultipleObjects(1, &h, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        quit = true;
        quitCode = int(msg.wParam);
        continue;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  dialog.join();
  if (quit) PostQuitMessage(quitCode);  // hand the quit request back to the main loop
  return result;
}

}  // namespace clipper
