#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

namespace clipper {

enum class ImageFormat { Png, Jpeg };

// WIC based image files. Pixels are 32-bit BGRA.
bool writeImage(const std::filesystem::path& file, const uint8_t* bgra, int width, int height, int stride, ImageFormat fmt);
bool readImage(const std::filesystem::path& file, std::vector<uint8_t>& bgra, int& width, int& height);

}  // namespace clipper
