#include "tilemap.h"

#include <cstdio>
#include <algorithm>
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
    int version = 0;
    if (r.ok && memcmp(magic, "SMP1", 4) == 0) version = 1;
    if (r.ok && memcmp(magic, "SMP2", 4) == 0) version = 2;
    if (!version) {
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

    warps.clear();
    actions.clear();
    outdoors = true;
    if (version >= 2) {
        outdoors = r.u8() != 0;
        int nw = r.u16();
        for (int i = 0; i < nw && r.ok; i++) {
            Warp wp;
            wp.x = (int16_t)r.u16();
            wp.y = (int16_t)r.u16();
            int len = r.u8();
            wp.target.resize(len);
            r.bytes(&wp.target[0], len);
            wp.tx = (int16_t)r.u16();
            wp.ty = (int16_t)r.u16();
            warps.push_back(wp);
        }
        int na = r.u16();
        for (int i = 0; i < na && r.ok; i++) {
            uint32_t cell = (uint32_t)r.u16() | ((uint32_t)r.u16() << 16);
            int len = r.u16();
            std::string a(len, ' ');
            r.bytes(&a[0], len);
            actions.push_back({cell, a});
        }
        std::sort(actions.begin(), actions.end());
    }
    props.clear();
    if (r.ok && r.pos + 6 <= data.size() && memcmp(&data[r.pos], "PRP1", 4) == 0) {
        r.pos += 4;
        int n = r.u16();
        for (int i = 0; i < n && r.ok; i++) {
            std::string k(r.u8(), ' ');
            r.bytes(&k[0], k.size());
            std::string v(r.u16(), ' ');
            r.bytes(&v[0], v.size());
            props[k] = v;
        }
    }
    overlays.clear();
    for (auto& v : overlayRow) v.assign((size_t)h + 1, 0);
    if (r.ok && r.pos + 8 <= data.size() && memcmp(&data[r.pos], "OVL1", 4) == 0) {
        r.pos += 4;
        uint32_t n = (uint32_t)r.u16() | ((uint32_t)r.u16() << 16);
        for (uint32_t i = 0; i < n && r.ok; i++) {
            Overlay o;
            o.group = r.u8();
            uint32_t cell = (uint32_t)r.u16() | ((uint32_t)r.u16() << 16);
            o.ts = r.u8();
            o.tile = r.u16();
            if (o.group > 3 || cell >= (uint32_t)w * h || o.ts >= tilesets.size()) continue;
            o.x = (uint16_t)(cell % w);
            o.y = (uint16_t)(cell / w);
            overlays.push_back(o);
        }
        rowRuns();
    }
    if (!r.ok) {
        if (err) *err = "truncated map file " + path;
        return false;
    }
    return true;
}

const std::string& TileMap::action(int x, int y) const {
    static const std::string none;
    if (!inBounds(x, y)) return none;
    uint32_t cell = (uint32_t)(y * w + x);
    auto it = std::lower_bound(actions.begin(), actions.end(), std::make_pair(cell, std::string()));
    if (it != actions.end() && it->first == cell) return it->second;
    return none;
}

const Warp* TileMap::warpAt(int x, int y) const {
    for (auto& wp : warps)
        if (wp.x == x && wp.y == y) return &wp;
    return nullptr;
}

const Warp* TileMap::warpTo(const std::string& target) const {
    for (auto& wp : warps)
        if (wp.target == target) return &wp;
    return nullptr;
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

const std::string& TileMap::prop(const std::string& key) const {
    static const std::string none;
    auto it = props.find(key);
    return it == props.end() ? none : it->second;
}

void TileMap::applyTileSwaps(const std::string& list) {
    static const char* names[L_COUNT] = {"Back", "Buildings", "Paths", "Front", "AlwaysFront"};
    std::vector<std::string> t;
    std::string cur;
    for (char ch : list) {
        if (ch == ' ') { if (!cur.empty()) t.push_back(cur); cur.clear(); }
        else cur += ch;
    }
    if (!cur.empty()) t.push_back(cur);
    for (size_t i = 0; i + 3 < t.size(); i += 4) {
        int layer = -1;
        for (int L = 0; L < L_COUNT; L++)
            if (t[i] == names[L]) layer = L;
        if (layer < 0) continue;
        setTile(layer, atoi(t[i + 1].c_str()), atoi(t[i + 2].c_str()), (uint16_t)atoi(t[i + 3].c_str()));
    }
}

} // namespace sv

namespace sv {

bool TileMap::findFit(const TileMap& p, int& bx, int& by, int* fitScore) const {
    if (p.w > w || p.h > h) return false;
    // translate the patch's tilesheets to ours by texture name (-1: we don't have it)
    // (a map can list the same sheet twice: SVE's mountain has the outdoor sheet at 0 and 6, so sheets are
    // compared by the first entry with their texture)
    std::vector<int> tsMap(p.tilesets.size(), -1), canon(tilesets.size());
    for (size_t j = 0; j < tilesets.size(); j++) {
        canon[j] = (int)j;
        for (size_t k = 0; k < j; k++) if (tilesets[k].texture == tilesets[j].texture) { canon[j] = (int)k; break; }
    }
    for (size_t i = 0; i < p.tilesets.size(); i++)
        for (size_t j = 0; j < tilesets.size(); j++)
            if (tilesets[j].texture == p.tilesets[i].texture) { tsMap[i] = (int)j; break; }
    int best = -1, total = 0;
    for (int L = 0; L < L_COUNT; L++)
        for (int i = 0; i < p.w * p.h; i++) total += p.tiles[L][i] != NO_TILE;
    for (int oy = 0; oy + p.h <= h; oy++)
        for (int ox = 0; ox + p.w <= w; ox++) {
            int score = 0;
            for (int L = 0; L < L_COUNT; L++)
                for (int y = 0; y < p.h; y++) {
                    const uint16_t* pt = &p.tiles[L][y * p.w];
                    const uint8_t* ps = &p.tsIdx[L][y * p.w];
                    const uint16_t* mt = &tiles[L][(oy + y) * w + ox];
                    const uint8_t* ms = &tsIdx[L][(oy + y) * w + ox];
                    for (int x = 0; x < p.w; x++)
                        if (pt[x] != NO_TILE && pt[x] == mt[x] && ms[x] < canon.size() && tsMap[ps[x]] == canon[ms[x]]) score++;
                }
            if (score > best) { best = score; bx = ox; by = oy; }
        }
    if (fitScore) *fitScore = total ? best * 100 / total : 0;
    return total > 0 && best * 4 >= total;   // at least a quarter of the ground lines up
}

bool TileMap::stamp(const TileMap& p, int dx, int dy) {
    if (dx < 0 || dy < 0 || dx + p.w > w || dy + p.h > h) return false;
    std::vector<int> tsMap(p.tilesets.size(), -1);
    for (size_t i = 0; i < p.tilesets.size(); i++) {
        for (size_t j = 0; j < tilesets.size(); j++)
            if (tilesets[j].texture == p.tilesets[i].texture) { tsMap[i] = (int)j; break; }
        if (tsMap[i] < 0 && tilesets.size() < 255) {
            tilesets.push_back(p.tilesets[i]);
            tsMap[i] = (int)tilesets.size() - 1;
        }
    }
    bool hasBack = false;
    for (int i = 0; i < p.w * p.h; i++) hasBack |= p.tiles[L_BACK][i] != NO_TILE;
    for (int y = 0; y < p.h; y++)
        for (int x = 0; x < p.w; x++) {
            int pi = y * p.w + x, mi = (dy + y) * w + dx + x;
            bool any = false;
            for (int L = 0; L < L_COUNT; L++) any |= p.tiles[L][pi] != NO_TILE;
            if (!any) continue;                       // a hole in the patch leaves the map alone
            for (int L = 0; L < L_COUNT; L++) {
                // the ground is never dug out; a patch with no ground of its own only adds to what's there
                if (p.tiles[L][pi] == NO_TILE && (L == L_BACK || !hasBack)) continue;
                int ts = p.tiles[L][pi] == NO_TILE ? 0 : tsMap[p.tsIdx[L][pi]];
                if (ts < 0) continue;
                tiles[L][mi] = p.tiles[L][pi];
                tsIdx[L][mi] = (uint8_t)ts;
            }
            if (hasBack || p.tiles[L_BUILDINGS][pi] != NO_TILE) cellFlags[mi] = p.cellFlags[pi];
            uint32_t cell = (uint32_t)mi;
            auto it = std::lower_bound(actions.begin(), actions.end(), std::make_pair(cell, std::string()));
            if (it != actions.end() && it->first == cell) actions.erase(it);
        }
    for (auto& a : p.actions) {
        int x = (int)(a.first % (uint32_t)p.w), y = (int)(a.first / (uint32_t)p.w);
        actions.push_back({(uint32_t)((dy + y) * w + dx + x), a.second});
    }
    std::sort(actions.begin(), actions.end());
    for (auto pw : p.warps) {
        pw.x += dx; pw.y += dy;
        bool have = false;
        for (auto& mw : warps) have |= mw.x == pw.x && mw.y == pw.y;
        if (!have) warps.push_back(pw);
    }
    // numbered layers: the patch's replace the map's on the cells it covers
    if (!p.overlays.empty() || !overlays.empty()) {
        auto covers = [&](int x, int y) {
            int px = x - dx, py = y - dy;
            if (px < 0 || py < 0 || px >= p.w || py >= p.h) return false;
            for (int L = 0; L < L_COUNT; L++) if (p.tiles[L][py * p.w + px] != NO_TILE) return true;
            return false;
        };
        std::vector<Overlay> keep;
        for (auto& o : overlays) if (!covers(o.x, o.y)) keep.push_back(o);
        for (auto o : p.overlays) {
            if (o.ts >= tsMap.size() || tsMap[o.ts] < 0) continue;
            o.ts = (uint8_t)tsMap[o.ts]; o.x = (uint16_t)(o.x + dx); o.y = (uint16_t)(o.y + dy);
            keep.push_back(o);
        }
        std::stable_sort(keep.begin(), keep.end(), [](const Overlay& a, const Overlay& b) {
            if (a.group != b.group) return a.group < b.group;
            if (a.y != b.y) return a.y < b.y;
            return a.x < b.x;
        });
        overlays = keep;
        rowRuns();
    }
    rev++;
    return true;
}

} // namespace sv

namespace sv {
void TileMap::setAction(int x, int y, const std::string& a) {
    if (!inBounds(x, y)) return;
    uint32_t cell = (uint32_t)(y * w + x);
    auto it = std::lower_bound(actions.begin(), actions.end(), std::make_pair(cell, std::string()));
    if (it != actions.end() && it->first == cell) it->second = a;
    else actions.insert(it, {cell, a});
}
} // namespace sv

namespace sv {
// overlays are kept in order (group, row, column); each group's rows are runs
void TileMap::rowRuns() {
    for (auto& v : overlayRow) v.assign((size_t)h + 1, 0);
    size_t at = 0;
    for (int gidx = 0; gidx < 4; gidx++)
        for (int y = 0; y <= h; y++) {
            while (at < overlays.size() && (overlays[at].group < gidx || (overlays[at].group == gidx && overlays[at].y < y))) at++;
            overlayRow[gidx][(size_t)y] = (uint32_t)at;
        }
}

bool TileMap::copyCells(const TileMap& p, const std::vector<uint8_t>& mask) {
    if (p.w != w || p.h != h || mask.size() != (size_t)w * h) return false;
    std::vector<int> tsMap(p.tilesets.size(), -1);
    for (size_t i = 0; i < p.tilesets.size(); i++) {
        for (size_t j = 0; j < tilesets.size(); j++)
            if (tilesets[j].texture == p.tilesets[i].texture) { tsMap[i] = (int)j; break; }
        if (tsMap[i] < 0 && tilesets.size() < 255) {
            tilesets.push_back(p.tilesets[i]);
            tsMap[i] = (int)tilesets.size() - 1;
        }
    }
    for (size_t i = 0; i < mask.size(); i++) {
        if (!mask[i]) continue;
        for (int L = 0; L < L_COUNT; L++) {
            tiles[L][i] = p.tiles[L][i];
            int ts = p.tiles[L][i] == NO_TILE ? 0 : tsMap[p.tsIdx[L][i]];
            tsIdx[L][i] = (uint8_t)std::max(0, ts);
            if (ts < 0) tiles[L][i] = NO_TILE;
        }
        cellFlags[i] = p.cellFlags[i];
        auto it = std::lower_bound(actions.begin(), actions.end(), std::make_pair((uint32_t)i, std::string()));
        if (it != actions.end() && it->first == (uint32_t)i) actions.erase(it);
    }
    for (auto& a : p.actions)
        if (a.first < mask.size() && mask[a.first]) actions.push_back(a);
    std::sort(actions.begin(), actions.end());
    std::vector<Overlay> keep;
    for (auto& o : overlays) if (!mask[(size_t)o.y * w + o.x]) keep.push_back(o);
    for (auto o : p.overlays)
        if (mask[(size_t)o.y * w + o.x] && tsMap[o.ts] >= 0) { o.ts = (uint8_t)tsMap[o.ts]; keep.push_back(o); }
    std::stable_sort(keep.begin(), keep.end(), [](const Overlay& a, const Overlay& b) {
        if (a.group != b.group) return a.group < b.group;
        if (a.y != b.y) return a.y < b.y;
        return a.x < b.x;
    });
    overlays = keep;
    rowRuns();
    rev++;
    return true;
}
} // namespace sv

namespace sv {
TileMap TileMap::trimmed(int& ox, int& oy) const {
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            for (int L = 0; L < L_COUNT; L++)
                if (tiles[L][y * w + x] != NO_TILE) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
    for (auto& o : overlays) { x0 = std::min(x0, (int)o.x); y0 = std::min(y0, (int)o.y); x1 = std::max(x1, (int)o.x); y1 = std::max(y1, (int)o.y); }
    TileMap t;
    if (x1 < 0) { ox = oy = 0; return t; }
    ox = x0; oy = y0;
    t.w = x1 - x0 + 1; t.h = y1 - y0 + 1;
    t.tilesets = tilesets; t.props = props; t.outdoors = outdoors; t.name = name;
    for (int L = 0; L < L_COUNT; L++) { t.tiles[L].assign((size_t)t.w * t.h, NO_TILE); t.tsIdx[L].assign((size_t)t.w * t.h, 0); }
    t.cellFlags.assign((size_t)t.w * t.h, 0);
    for (int y = 0; y < t.h; y++)
        for (int x = 0; x < t.w; x++) {
            size_t si = (size_t)(y + y0) * w + x + x0, di = (size_t)y * t.w + x;
            for (int L = 0; L < L_COUNT; L++) { t.tiles[L][di] = tiles[L][si]; t.tsIdx[L][di] = tsIdx[L][si]; }
            t.cellFlags[di] = cellFlags[si];
        }
    for (auto& a : actions) {
        int x = (int)(a.first % (uint32_t)w) - x0, y = (int)(a.first / (uint32_t)w) - y0;
        if (x >= 0 && y >= 0 && x < t.w && y < t.h) t.actions.push_back({(uint32_t)(y * t.w + x), a.second});
    }
    for (auto o : overlays) { o.x = (uint16_t)(o.x - x0); o.y = (uint16_t)(o.y - y0); t.overlays.push_back(o); }
    t.rowRuns();
    for (auto wp : warps) {
        wp.x -= x0; wp.y -= y0;
        if (wp.x >= 0 && wp.y >= 0 && wp.x < t.w && wp.y < t.h) t.warps.push_back(wp);
    }
    return t;
}
void TileMap::addOverlay(int x, int y, uint16_t tile, int ts, int group) {
    if (!inBounds(x, y) || group < 0 || group > 3 || ts < 0 || ts >= (int)tilesets.size()) return;
    overlays.push_back({(uint16_t)x, (uint16_t)y, tile, (uint8_t)ts, (uint8_t)group});
    std::stable_sort(overlays.begin(), overlays.end(), [](const Overlay& a, const Overlay& b) {
        if (a.group != b.group) return a.group < b.group;
        if (a.y != b.y) return a.y < b.y;
        return a.x < b.x;
    });
    rowRuns();
    rev++;
}
} // namespace sv
