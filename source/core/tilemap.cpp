#include "tilemap.h"

#include <cstdio>
#include <cstring>

namespace sv {

std::string Tileset::textureFor(int season) const {
    static const char* names[] = {"spring", "summer", "fall", "winter"};
    std::string t = texture;
    size_t p = t.find("{S}");
    if (p != std::string::npos) t.replace(p, 3, names[season & 3]);
    return t;
}

namespace {
struct Reader {
    const std::vector<uint8_t>& d;
    size_t pos = 0;
    bool ok = true;
    explicit Reader(const std::vector<uint8_t>& data) : d(data) {}
    bool need(size_t n) {
        if (pos + n > d.size()) ok = false;
        return ok;
    }
    uint8_t u8() { return need(1) ? d[pos++] : 0; }
    uint16_t u16() {
        if (!need(2)) return 0;
        uint16_t v = (uint16_t)(d[pos] | (d[pos + 1] << 8));
        pos += 2;
        return v;
    }
    void bytes(void* out, size_t n) {
        if (!need(n)) return;
        memcpy(out, &d[pos], n);
        pos += n;
    }
};
} // namespace

bool TileMap::load(const std::string& path, std::string* err) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        if (err) *err = "can't open " + path;
        return false;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> data(sz > 0 ? (size_t)sz : 0);
    if (sz > 0 && fread(data.data(), 1, data.size(), f) != data.size()) {
        fclose(f);
        if (err) *err = "read error " + path;
        return false;
    }
    fclose(f);

    Reader r(data);
    char magic[4];
    r.bytes(magic, 4);
    if (!r.ok || memcmp(magic, "SMP1", 4) != 0) {
        if (err) *err = "bad map file " + path;
        return false;
    }
    w = r.u16();
    h = r.u16();
    int nts = r.u8();
    tilesets.assign(nts, Tileset());
    for (int i = 0; i < nts; i++) {
        Tileset& ts = tilesets[i];
        int len = r.u8();
        ts.texture.resize(len);
        r.bytes(&ts.texture[0], len);
        ts.columns = r.u16();
        ts.tileCount = r.u16();
        int nanim = r.u16();
        ts.animIndex.assign(ts.tileCount > 0 ? ts.tileCount : 1, -1);
        for (int a = 0; a < nanim; a++) {
            uint16_t tid = r.u16();
            int nf = r.u8();
            TileAnim an;
            an.durationMs = r.u16();
            if (an.durationMs == 0) an.durationMs = 250;
            for (int k = 0; k < nf; k++) an.frames.push_back(r.u16());
            if (tid < ts.animIndex.size() && !an.frames.empty()) {
                ts.animIndex[tid] = (int16_t)ts.anims.size();
                ts.anims.push_back(an);
            }
        }
    }
    int nl = r.u8();
    size_t cells = (size_t)w * h;
    for (int l = 0; l < L_COUNT; l++) {
        tiles[l].assign(cells, NO_TILE);
        tsIdx[l].assign(cells, 0);
    }
    for (int l = 0; l < nl; l++) {
        std::vector<uint16_t> t(cells);
        std::vector<uint8_t> s(cells);
        for (size_t i = 0; i < cells; i++) t[i] = r.u16();
        r.bytes(s.data(), cells);
        if (l < L_COUNT) {
            tiles[l].swap(t);
            tsIdx[l].swap(s);
        }
    }
    cellFlags.assign(cells, 0);
    r.bytes(cellFlags.data(), cells);
    if (!r.ok) {
        if (err) *err = "truncated map file " + path;
        return false;
    }
    return true;
}

uint16_t TileMap::animatedTile(int ts, uint16_t t, uint32_t timeMs) const {
    if (ts < 0 || ts >= (int)tilesets.size()) return t;
    const Tileset& s = tilesets[ts];
    if (t >= s.animIndex.size()) return t;
    int ai = s.animIndex[t];
    if (ai < 0) return t;
    const TileAnim& a = s.anims[ai];
    return a.frames[(timeMs / a.durationMs) % a.frames.size()];
}

} // namespace sv
