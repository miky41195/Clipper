#pragma once
#include <windows.h>
#include <filesystem>
#include <string>

namespace clipper {

// Renames keeping the extension; the new stem is sanitized and made unique. Returns the new path.
std::filesystem::path renameClip(const std::filesystem::path& file, const std::string& newStem);
bool recycle(const std::filesystem::path& file);  // to the Recycle Bin
void revealInExplorer(const std::filesystem::path& file);
bool copyFileToClipboard(const std::filesystem::path& file, HWND owner);  // as a file (CF_HDROP), pasteable in Discord/Explorer

}  // namespace clipper
