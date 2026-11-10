// Writes the engine's .s3t texture format (same bytes as tools/convert.py).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace s3t {
// rgba: straight-alpha RGBA8, top row first.
bool write(const std::string& path, int w, int h, const std::vector<uint8_t>& rgba);
bool encode(int w, int h, const std::vector<uint8_t>& rgba, std::vector<uint8_t>& out);
}
