#include "setup/xnb.h"
#include "setup/lzx.h"
#include <cstdio>
#include <cstring>

namespace xnb {

uint8_t File::u8() { return pos < data.size() ? data[pos++] : (pos++, 0); }
uint32_t File::u32() {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= (uint32_t)u8() << (8 * i);
    return v;
}
int32_t File::i32() { return (int32_t)u32(); }
float File::f32() { uint32_t v = u32(); float f; memcpy(&f, &v, 4); return f; }
uint32_t File::v7() {
    uint32_t v = 0; int shift = 0;
    for (int i = 0; i < 5; i++) {
        uint8_t b = u8();
        v |= (uint32_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) break;
        shift += 7;
    }
    return v;
}
std::string File::str() {
    uint32_t n = v7();
    if (pos + n > data.size()) { pos = data.size() + 1; return ""; }
    std::string s((const char*)&data[pos], n);
    pos += n;
    return s;
}
bool File::bytes(void* dst, size_t n) {
    if (pos + n > data.size()) { pos = data.size() + 1; return false; }
    memcpy(dst, &data[pos], n);
    pos += n;
    return true;
}
std::string File::typeId() {
    uint32_t id = v7();
    if (id == 0 || id > readers.size()) return "";
    return readers[id - 1];
}

bool lz4Block(const uint8_t* in, size_t inSize, std::vector<uint8_t>& out, size_t outSize) {
    out.clear();
    out.reserve(outSize);
    size_t i = 0;
    while (i < inSize) {
        uint8_t tok = in[i++];
        size_t lit = tok >> 4;
        if (lit == 15) { uint8_t b; do { if (i >= inSize) return false; b = in[i++]; lit += b; } while (b == 255); }
        if (i + lit > inSize) return false;
        out.insert(out.end(), in + i, in + i + lit);
        i += lit;
        if (i >= inSize) break;                 // last sequence has no match
        if (i + 2 > inSize) return false;
        size_t off = in[i] | (in[i + 1] << 8);
        i += 2;
        if (off == 0 || off > out.size()) return false;
        size_t len = tok & 15;
        if (len == 15) { uint8_t b; do { if (i >= inSize) return false; b = in[i++]; len += b; } while (b == 255); }
        len += 4;
        size_t src = out.size() - off;
        for (size_t k = 0; k < len; k++) out.push_back(out[src + k]);
    }
    return out.size() == outSize;
}

bool loadMemory(const uint8_t* buf, size_t size, File& f) {
    f = File();
    if (size < 10 || memcmp(buf, "XNB", 3) != 0) { f.error = "not an XNB file"; return false; }
    uint8_t flags = buf[5];
    uint32_t fileSize = buf[6] | (buf[7] << 8) | (buf[8] << 16) | ((uint32_t)buf[9] << 24);
    if (fileSize > size) fileSize = (uint32_t)size;
    if (flags & 0xC0) {
        if (size < 14) { f.error = "truncated"; return false; }
        uint32_t outSize = buf[10] | (buf[11] << 8) | (buf[12] << 16) | ((uint32_t)buf[13] << 24);
        const uint8_t* body = buf + 14;
        size_t bodySize = fileSize - 14;
        bool ok = (flags & 0x80) ? lzx::decompressXnb(body, bodySize, f.data, outSize)
                                 : lz4Block(body, bodySize, f.data, outSize);
        if (!ok) { f.error = (flags & 0x80) ? "LZX data is damaged" : "LZ4 data is damaged"; return false; }
    } else {
        f.data.assign(buf + 10, buf + fileSize);
    }
    uint32_t n = f.v7();
    if (n > 64) { f.error = "bad reader table"; return false; }
    for (uint32_t i = 0; i < n; i++) {
        f.readers.push_back(f.str());
        f.i32();
    }
    f.v7();   // shared resource count (unused by Stardew)
    if (f.pos > f.data.size()) { f.error = "truncated"; return false; }
    return true;
}

bool load(const std::string& path, File& f) {
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) { f = File(); f.error = "can't open " + path; return false; }
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    std::vector<uint8_t> buf(n > 0 ? (size_t)n : 0);
    size_t got = n > 0 ? fread(buf.data(), 1, (size_t)n, fp) : 0;
    fclose(fp);
    if (got != buf.size()) { f = File(); f.error = "read error"; return false; }
    return loadMemory(buf.data(), buf.size(), f);
}

std::string primaryType(File& f) {
    size_t save = f.pos;
    std::string t = f.typeId();
    f.pos = save;
    return t;
}

static bool startsWith(const std::string& s, const char* p) { return s.compare(0, strlen(p), p) == 0; }

bool readTexture(File& f, int& w, int& h, std::vector<uint8_t>& rgba) {
    std::string t = f.typeId();
    if (t.find("Texture2DReader") == std::string::npos) { f.error = "not a texture"; return false; }
    int32_t fmt = f.i32();
    w = (int)f.u32();
    h = (int)f.u32();
    uint32_t mips = f.u32();
    if (fmt != 0 || mips < 1) { f.error = "unsupported texture format"; return false; }
    uint32_t sz = f.u32();
    if (sz != (uint32_t)(w * h * 4)) { f.error = "texture size mismatch"; return false; }
    rgba.resize(sz);
    if (!f.bytes(rgba.data(), sz)) { f.error = "truncated texture"; return false; }
    // XNA content is premultiplied; the engine wants straight alpha
    for (size_t i = 0; i < sz; i += 4) {
        uint8_t a = rgba[i + 3];
        if (a == 0) { rgba[i] = rgba[i + 1] = rgba[i + 2] = 0; continue; }
        if (a == 255) continue;
        for (int c = 0; c < 3; c++) {
            unsigned v = (rgba[i + c] * 255u + a / 2) / a;
            rgba[i + c] = (uint8_t)(v > 255 ? 255 : v);
        }
    }
    return true;
}

// Reads one value whose static type is string or int (as text).
static bool readKeyOrValue(File& f, const std::string& elemReader, std::string& out) {
    if (elemReader.find("Int32Reader") != std::string::npos) {
        out = std::to_string(f.i32());
        return true;
    }
    std::string t = f.typeId();      // strings are reference types: type id first
    if (t.empty()) { out.clear(); return true; }
    if (t.find("StringReader") == std::string::npos) { f.error = "unexpected element " + t; return false; }
    out = f.str();
    return true;
}

bool readStringDict(File& f, std::vector<std::pair<std::string, std::string>>& out) {
    std::string t = f.typeId();
    if (t.find("DictionaryReader") == std::string::npos) { f.error = "not a dictionary"; return false; }
    // generic args decide whether keys are ints or strings
    bool intKey = t.find("[[System.Int32") != std::string::npos;
    if (!startsWith(t.substr(t.find("[[") + 2), "System.Int32") && !startsWith(t.substr(t.find("[[") + 2), "System.String")) {
        f.error = "dictionary keys aren't text";
        return false;
    }
    size_t second = t.find("],[");
    if (second == std::string::npos || !startsWith(t.substr(second + 3), "System.String")) {
        f.error = "dictionary values aren't text";
        return false;
    }
    uint32_t n = f.u32();
    out.clear();
    out.reserve(n);
    std::string keyReader = intKey ? "Int32Reader" : "StringReader";
    for (uint32_t i = 0; i < n; i++) {
        std::string k, v;
        if (!readKeyOrValue(f, keyReader, k)) return false;
        if (!readKeyOrValue(f, "StringReader", v)) return false;
        if (f.pos > f.data.size()) { f.error = "truncated dictionary"; return false; }
        out.emplace_back(std::move(k), std::move(v));
    }
    return true;
}

bool readTbin(File& f, std::vector<uint8_t>& tbin) {
    std::string t = f.typeId();
    if (t.find("TideReader") == std::string::npos) { f.error = "not a map"; return false; }
    int32_t n = f.i32();
    if (n <= 0) { f.error = "empty map"; return false; }
    tbin.resize((size_t)n);
    if (!f.bytes(tbin.data(), (size_t)n)) { f.error = "truncated map"; return false; }
    return true;
}

}  // namespace xnb
