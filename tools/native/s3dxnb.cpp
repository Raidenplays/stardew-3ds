// Tiny C API over the 3DS setup's XNB decompressors, so the Python converter
// (PC or the Android app) can read the game's packed .xnb files quickly.
#include <cstring>
#include <vector>

#include "setup/lzx.h"
#include "setup/xnb.h"

extern "C" {

// body = the XNB bytes after the 14-byte header; lz4 != 0 for flag 0x40.
// Writes exactly outLen bytes to out. Returns 1 on success, 0 on bad data.
int s3d_xnb_decompress(const unsigned char* body, size_t bodyLen, int lz4, unsigned char* out, size_t outLen) {
    std::vector<uint8_t> v;
    bool ok = lz4 ? xnb::lz4Block(body, bodyLen, v, outLen) : lzx::decompressXnb(body, bodyLen, v, outLen);
    if (!ok || v.size() != outLen) return 0;
    memcpy(out, v.data(), outLen);
    return 1;
}

}
