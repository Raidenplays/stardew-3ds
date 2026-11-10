// LZX decompressor for XNA/MonoGame XNB files (the format Stardew Valley's
// Content/*.xnb files are packed with). Written from the published LZX
// format description; no third-party code.
#pragma once
#include <cstdint>
#include <vector>

namespace lzx {

// Decompresses the payload of an XNB with the 0x80 flag (everything after
// the "decompressed size" field) into exactly outSize bytes.
// Returns false on corrupt data.
bool decompressXnb(const uint8_t* in, size_t inSize, std::vector<uint8_t>& out, size_t outSize);

}  // namespace lzx
