#include "texture.h"

#include <cstdio>
#include <cstring>

Texture::~Texture() {
    for (auto& p : pages) C3D_TexDelete(&p.tex);
}

bool Texture::load(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char magic[4];
    u16 hdr[4];
    if (fread(magic, 1, 4, f) != 4 || (memcmp(magic, "S3T1", 4) != 0 && memcmp(magic, "S3T2", 4) != 0) ||
        fread(hdr, 2, 4, f) != 4) {
        fclose(f);
        return false;
    }
    bool v2 = magic[3] == '2';
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
        if (!C3D_TexInit(&p.tex, p.pw, p.ph, GPU_RGBA8)) {
            fclose(f);
            return false;   // out of linear memory
        }
        size_t bytes = (size_t)p.pw * p.ph * 4;
        if (fread(p.tex.data, 1, bytes, f) != bytes) {
            C3D_TexDelete(&p.tex);
            fclose(f);
            return false;
        }
        C3D_TexFlush(&p.tex);
        C3D_TexSetFilter(&p.tex, GPU_NEAREST, GPU_NEAREST);
        C3D_TexSetWrap(&p.tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        pages.push_back(p);
    }
    fclose(f);
    return !pages.empty();
}

void Texture::draw(int sx, int sy, int sw, int sh, float dx, float dy, bool flipX,
                   const C2D_ImageTint* tint, float scale) const {
    for (const TexPage& p : pages) {
        if (sy < p.y0 || sy + sh > p.y0 + p.h || sx < p.x0 || sx + sw > p.x0 + p.w) continue;
        float ly = (float)(sy - p.y0), lx = (float)(sx - p.x0);
        Tex3DS_SubTexture sub;
        sub.width = (u16)sw;
        sub.height = (u16)sh;
        float l = lx / p.pw, r = (lx + sw) / p.pw;
        sub.left = flipX ? r : l;
        sub.right = flipX ? l : r;
        sub.top = 1.0f - ly / p.ph;
        sub.bottom = 1.0f - (ly + sh) / p.ph;
        C2D_Image img = {const_cast<C3D_Tex*>(&p.tex), &sub};
        C2D_DrawImageAt(img, dx, dy, 0.5f, tint, scale, scale);
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

void TexCache::clear() { cache.clear(); }
