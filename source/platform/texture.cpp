#include "texture.h"

#include <cstdio>
#include <cstring>
#include <vector>

extern "C" int s3d_inflate(char* out, int outLen, const char* in, int inLen);

// The converters store each page bottom row first, but the GPU (like tex3ds)
// wants the top row first. Flip in place: tile row r swaps with tile row
// TH-1-r, and inside a tile y -> 7-y, which flips the three y bits of the
// Morton index (bits 1, 3, 5 -> XOR 0x2A).
static void unflip(u32* px, int pw, int ph) {
    int tw = pw / 8, th = ph / 8;
    for (int r = 0; r < (th + 1) / 2; r++) {
        int r2 = th - 1 - r;
        for (int c = 0; c < tw; c++) {
            u32* a = px + ((size_t)(r * tw + c) << 6);
            u32* b = px + ((size_t)(r2 * tw + c) << 6);
            for (int m = 0; m < 64; m++) {
                int m2 = m ^ 0x2A;
                if (a == b && m2 <= m) continue;   // middle tile row: swap once
                u32 t = a[m];
                a[m] = b[m2];
                b[m2] = t;
            }
        }
    }
}

static u32 g_frame = 1;
static std::vector<Texture*> g_textures;   // every live texture, for eviction

void texNewFrame() { g_frame++; }

Texture::~Texture() {
    for (auto& p : pages)
        if (p.loaded) C3D_TexDelete(&p.tex);
    for (size_t i = 0; i < g_textures.size(); i++)
        if (g_textures[i] == this) {
            g_textures[i] = g_textures.back();
            g_textures.pop_back();
            break;
        }
}

// Read the header and page table only; pixels load when a page is drawn.
bool Texture::load(const std::string& path_) {
    FILE* f = fopen(path_.c_str(), "rb");
    if (!f) return false;
    char magic[4];
    u16 hdr[4];
    if (fread(magic, 1, 4, f) != 4 || (memcmp(magic, "S3T1", 4) != 0 && memcmp(magic, "S3T2", 4) != 0 && memcmp(magic, "S3T3", 4) != 0) ||
        fread(hdr, 2, 4, f) != 4) {
        fclose(f);
        return false;
    }
    path = path_;
    v2 = magic[3] >= '2';
    packed = magic[3] == '3';   // S3T3: each page deflated
    width = hdr[0];
    height = hdr[1];
    int count = hdr[2];
    for (int i = 0; i < count; i++) {
        u16 ph[6] = {0, 0, 0, 0, 0, 0};
        TexPage p;
        if (v2) {
            if (fread(ph, 2, 6, f) != 6) break;
            p.x0 = ph[0]; p.y0 = ph[1]; p.w = ph[2]; p.h = ph[3]; p.pw = ph[4]; p.ph = ph[5];
        } else {
            if (fread(ph, 2, 4, f) != 4) break;
            p.y0 = ph[0]; p.h = ph[1]; p.pw = ph[2]; p.ph = ph[3]; p.w = width;
        }
        size_t bytes = (size_t)p.pw * p.ph * 4;
        if (packed) {
            u32 n = 0;
            if (fread(&n, 4, 1, f) != 1 || n >= (64u << 20)) break;
            p.compLen = n;
            p.fileOff = ftell(f);
            if (fseek(f, n, SEEK_CUR) != 0) break;
        } else {
            p.fileOff = ftell(f);
            if (fseek(f, (long)bytes, SEEK_CUR) != 0) break;
        }
        pages.push_back(p);
    }
    fclose(f);
    if (pages.empty()) return false;
    g_textures.push_back(this);
    return true;
}

bool Texture::evictOne() {
    TexPage* best = nullptr;
    for (Texture* t : g_textures)
        for (TexPage& p : t->pages)
            if (p.loaded && p.lastUsed < g_frame && (!best || p.lastUsed < best->lastUsed)) best = &p;
    if (!best) return false;
    C3D_TexDelete(&best->tex);
    best->loaded = false;
    return true;
}

bool Texture::ensure(TexPage& p) const {
    p.lastUsed = g_frame;
    if (p.loaded) return true;
    // citro2d remembers the last bound C3D_Tex by address; a page reloaded
    // into the same struct must not be mistaken for what was bound before.
    C2D_Flush();
    while (!C3D_TexInit(&p.tex, p.pw, p.ph, GPU_RGBA8))
        if (!evictOne()) return false;   // out of GPU memory even after evicting
    size_t bytes = (size_t)p.pw * p.ph * 4;
    bool ok = false;
    FILE* f = fopen(path.c_str(), "rb");
    if (f && fseek(f, p.fileOff, SEEK_SET) == 0) {
        if (packed) {
            std::vector<char> comp(p.compLen);
            ok = fread(comp.data(), 1, p.compLen, f) == p.compLen &&
                 s3d_inflate((char*)p.tex.data, (int)bytes, comp.data(), (int)p.compLen) == (int)bytes;
        } else {
            ok = fread(p.tex.data, 1, bytes, f) == bytes;
        }
    }
    if (f) fclose(f);
    if (!ok) {
        C3D_TexDelete(&p.tex);
        return false;
    }
    unflip((u32*)p.tex.data, p.pw, p.ph);
    C3D_TexFlush(&p.tex);
    C3D_TexSetFilter(&p.tex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&p.tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    p.loaded = true;
    C2D_Prepare();   // forgets the cached texture binding
    return true;
}

void Texture::draw(int sx, int sy, int sw, int sh, float dx, float dy, bool flipX,
                   const C2D_ImageTint* tint, float scale) const {
    // A sprite can straddle the seam between two 1024px pages (e.g. the title
    // mountains on Cursors), so draw the part that falls in each page.
    for (const TexPage& p : pages) {
        int x0 = sx > p.x0 ? sx : p.x0, y0 = sy > p.y0 ? sy : p.y0;
        int x1 = sx + sw < p.x0 + p.w ? sx + sw : p.x0 + p.w;
        int y1 = sy + sh < p.y0 + p.h ? sy + sh : p.y0 + p.h;
        if (x1 <= x0 || y1 <= y0) continue;
        if (!ensure(const_cast<TexPage&>(p))) continue;
        int pw = x1 - x0, ph = y1 - y0;
        float lx = (float)(x0 - p.x0), ly = (float)(y0 - p.y0);
        Tex3DS_SubTexture sub;
        sub.width = (u16)pw;
        sub.height = (u16)ph;
        float l = lx / p.pw, r = (lx + pw) / p.pw;
        sub.left = flipX ? r : l;
        sub.right = flipX ? l : r;
        sub.top = 1.0f - ly / p.ph;
        sub.bottom = 1.0f - (ly + ph) / p.ph;
        // where this piece lands on screen (mirrored across the sprite if flipped)
        float ox = flipX ? (float)((sx + sw) - x1) : (float)(x0 - sx);
        float oy = (float)(y0 - sy);
        C2D_Image img = {const_cast<C3D_Tex*>(&p.tex), &sub};
        C2D_DrawImageAt(img, dx + ox * scale, dy + oy * scale, 0.5f, tint, scale, scale);
    }
}

void Texture::drawRotated(int sx, int sy, int sw, int sh, float cx, float cy, float radians,
                          const C2D_ImageTint* tint) const {
    for (const TexPage& p : pages) {
        if (sy < p.y0 || sy + sh > p.y0 + p.h || sx < p.x0 || sx + sw > p.x0 + p.w) continue;
        if (!ensure(const_cast<TexPage&>(p))) return;
        float ly = (float)(sy - p.y0), lx = (float)(sx - p.x0);
        Tex3DS_SubTexture sub;
        sub.width = (u16)sw;
        sub.height = (u16)sh;
        sub.left = lx / p.pw;
        sub.right = (lx + sw) / p.pw;
        sub.top = 1.0f - ly / p.ph;
        sub.bottom = 1.0f - (ly + sh) / p.ph;
        C2D_Image img = {const_cast<C3D_Tex*>(&p.tex), &sub};
        C2D_DrawImageAtRotated(img, cx, cy, 0.5f, radians, tint, 1.0f, 1.0f);
        return;
    }
}

const Texture* TexCache::get(const std::string& name) {
    auto it = cache.find(name);
    if (it != cache.end()) return it->second.get();
    std::unique_ptr<Texture> t(new Texture());
    if (!t->load(resolve("tex/" + name + ".s3t"))) t.reset();
    const Texture* out = t.get();
    cache[name] = std::move(t);   // also caches misses as nullptr
    return out;
}

void TexCache::clear() {
    C2D_Flush();
    cache.clear();
    C2D_Prepare();
}
