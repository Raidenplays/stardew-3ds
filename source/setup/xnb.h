// Reads Stardew Valley's packed Content/*.xnb files directly, so the 3DS can
// convert the player's own game files on first launch without any PC tools.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace xnb {

struct File {
    std::vector<uint8_t> data;           // decompressed body (after the header)
    size_t pos = 0;
    std::vector<std::string> readers;    // type reader names, in file order
    std::string error;

    bool eof() const { return pos >= data.size(); }
    uint8_t u8();
    int32_t i32();
    uint32_t u32();
    float f32();
    uint32_t v7();                       // 7-bit encoded int
    std::string str();                   // 7-bit length + UTF-8
    bool bytes(void* dst, size_t n);
    // Reads a type id; returns the reader name ("" for null).
    std::string typeId();
};

// Loads and decompresses an .xnb. On success f.pos points at the primary
// object's type id.
bool load(const std::string& path, File& f);
bool loadMemory(const uint8_t* buf, size_t size, File& f);

// The primary object's reader name without consuming anything.
std::string primaryType(File& f);

// Texture2D -> straight-alpha RGBA8 (top row first).
bool readTexture(File& f, int& w, int& h, std::vector<uint8_t>& rgba);

// Dictionary<string,string> (and Dictionary<int,string>, keys as text).
bool readStringDict(File& f, std::vector<std::pair<std::string, std::string>>& out);

// xTile map (TideReader) -> the raw .tbin bytes inside.
bool readTbin(File& f, std::vector<uint8_t>& tbin);

struct Rect { int x, y, w, h; };
struct SpriteFont {
    int texW = 0, texH = 0;
    std::vector<uint8_t> rgba;          // straight alpha
    std::vector<Rect> glyphs, cropping;
    std::vector<uint32_t> chars;        // code points
    std::vector<float> kerning;         // 3 per glyph: left bearing, width, right bearing
    int lineSpacing = 0;
    float spacing = 0;
};
bool readSpriteFont(File& f, SpriteFont& out);

// LZ4 block decoder (XNB flag 0x40).
bool lz4Block(const uint8_t* in, size_t inSize, std::vector<uint8_t>& out, size_t outSize);

}  // namespace xnb
