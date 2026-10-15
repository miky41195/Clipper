#pragma once
#include <filesystem>
#include <string>

namespace clipper {

// Deletes leftover buffer segments and half-written clips; an interrupted recording
// ("*.rec.mkv.tmp") is kept as "*_recuperata.mkv".
void cleanupOnStartup(const std::filesystem::path& bufferDir, const std::filesystem::path& outputDir);

// Display name of the app owning the foreground window ("Desktop" for the shell).
std::string foregroundAppName();

}  // namespace clipper
