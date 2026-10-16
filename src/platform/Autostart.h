#pragma once
#include <string>

namespace clipper {

// HKCU\Software\Microsoft\Windows\CurrentVersion\Run\<valueName>
bool autostartEnabled(const wchar_t* valueName = L"Clipper");
void setAutostart(bool enabled, const std::wstring& exePath, const wchar_t* valueName = L"Clipper");

}  // namespace clipper
