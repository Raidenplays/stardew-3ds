#include "host_canvas.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb_truetype.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb_image_write.h"

static stbtt_fontinfo g_font;
static bool g_fontOk = false;

HostCanvas::HostCanvas(const std::string& r, const std::string& fontPath) : root(r) {
    FILE* f = fopen(fontPath.c_str(), "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        font.resize(ftell(f));
        fseek(f, 0, SEEK_SET);
        if (fread(font.data(), 1, font.size(), f) == font.size())
            g_fontOk = stbtt_InitFont(&g_font, font.data(), 0) != 0;
        fclose(f);
    }
}

void HostCanvas::resize(int w, int h) {
    W = w;
    H = h;
    fb.assign((size_t)w * h, 0x000000FF);
    cx = cy = 0;
    cw = w;
    ch = h;
}

// Undo convert.py's swizzle: Morton order inside 8x8 tiles, A,B,G,R bytes, rows flipped.
extern "C" int s3d_inflate(char* out, int outLen, const char* in, int inLen);

static bool loadS3T(const std::string& path, HostTexture& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char magic[4];
    uint16_t hdr[4];
    if (fread(magic, 1, 4, f) != 4 || (memcmp(magic, "S3T1", 4) && memcmp(magic, "S3T2", 4) && memcmp(magic, "S3T3", 4) && memcmp(magic, "S3T4", 4)) ||
        fread(hdr, 2, 4, f) != 4) {
        fclose(f);
        return false;
    }
    bool v2 = magic[3] >= '2', packed = magic[3] >= '3';
    out.w = hdr[0];
    out.h = hdr[1];
    out.px.assign((size_t)out.w * out.h, 0);
    for (int p = 0; p < hdr[2]; p++) {
        uint16_t ph[6] = {0, 0, 0, 0, 0, 0};
        int x0 = 0, y0, ww, hh, pw, pgh;
        if (v2) {
            if (fread(ph, 2, 6, f) != 6) break;
            x0 = ph[0]; y0 = ph[1]; ww = ph[2]; hh = ph[3]; pw = ph[4]; pgh = ph[5];
        } else {
            if (fread(ph, 2, 4, f) != 4) break;
            y0 = ph[0]; hh = ph[1]; pw = ph[2]; pgh = ph[3]; ww = out.w;
        }
        std::vector<uint8_t> data((size_t)pw * pgh * 4);
        if (packed) {
            uint32_t n = 0;
            if (fread(&n, 4, 1, f) != 1) break;
            std::vector<char> comp(n);
            if (fread(comp.data(), 1, n, f) != n) break;
            int got = s3d_inflate((char*)data.data(), (int)data.size(), comp.data(), (int)n);
            if (got != (int)data.size()) {   // S3T4: a palette, then one byte per pixel
                size_t npx = (size_t)pw * pgh;
                unsigned nc = got >= 2 ? (data[0] | (data[1] << 8)) : 0;
                if (nc < 1 || nc > 256 || (size_t)got != 2 + nc * 4 + npx) break;
                std::vector<uint8_t> pal(data.begin() + 2, data.begin() + 2 + nc * 4), idx(data.begin() + 2 + nc * 4, data.begin() + got);
                for (size_t i = 0; i < npx; i++) memcpy(&data[i * 4], &pal[(size_t)(idx[i] < nc ? idx[i] : 0) * 4], 4);
            }
        } else if (fread(data.data(), 1, data.size(), f) != data.size()) break;
        for (int gy = 0; gy < pgh; gy++)
            for (int x = 0; x < pw; x++) {
                int tile = (gy >> 3) * (pw >> 3) + (x >> 3);
                int xx = x & 7, yy = gy & 7;
                int morton = (xx & 1) | ((yy & 1) << 1) | ((xx & 2) << 1) | ((yy & 2) << 2) |
                             ((xx & 4) << 2) | ((yy & 4) << 3);
                size_t i = ((size_t)tile * 64 + morton) * 4;
                int imgY = (hdr[3] & 1) ? gy : pgh - 1 - gy;   // newer files are top row first
                if (imgY >= hh || x >= ww) continue;
                uint8_t a = data[i], b = data[i + 1], g = data[i + 2], r = data[i + 3];
                out.px[(size_t)(y0 + imgY) * out.w + x0 + x] =
                    ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | a;
            }
    }
    fclose(f);
    return true;
}

const HostTexture* HostCanvas::get(TexName tn) {
    std::string name = tn.str();
    auto it = cache.find(name);
    if (it != cache.end()) return it->second.w ? &it->second : nullptr;
    HostTexture& t = cache[name];
    std::string rel = "tex/" + name + ".s3t";
    std::string full = resolveFn ? resolveFn(rel, resolveCtx) : root + rel;
    if (!loadS3T(full, t)) t = HostTexture();
    return t.w ? &t : nullptr;
}

int HostCanvas::texWidth(TexName tex) {
    const HostTexture* t = get(tex);
    return t ? t->w : 0;
}

int HostCanvas::texHeight(TexName tex) {
    const HostTexture* t = get(tex);
    return t ? t->h : 0;
}

void HostCanvas::blend(int x, int y, uint32_t c) {
    x += ox;
    y += oy;
    if (x < cx || y < cy || x >= cx + cw || y >= cy + ch || x < 0 || y < 0 || x >= W || y >= H) return;
    uint32_t a = c & 0xFF;
    if (!a) return;
    uint32_t& d = fb[(size_t)y * W + x];
    if (a == 255) { d = c | 0xFF; return; }
    if (inCache && (d & 0xFF) == 0) { d = c; return; }   // onto see-through cache: keep its alpha
    auto ch_ = [&](int sh) {
        uint32_t s = (c >> sh) & 0xFF, dd = (d >> sh) & 0xFF;
        return ((s * a + dd * (255 - a)) / 255) << sh;
    };
    d = ch_(24) | ch_(16) | ch_(8) | 0xFF;
}

void HostCanvas::image(TexName tex, int sx, int sy, int sw, int sh, float dx, float dy,
                       bool flipX, uint32_t tint) {
    draws++;
    const HostTexture* t = get(tex);
    if (!t) return;
    int bx = (int)std::floor(dx), by = (int)std::floor(dy);
    for (int y = 0; y < sh; y++) {
        int ty = sy + y;
        if (ty < 0 || ty >= t->h) continue;
        for (int x = 0; x < sw; x++) {
            int tx = sx + (flipX ? sw - 1 - x : x);
            if (tx < 0 || tx >= t->w) continue;
            uint32_t c = t->px[(size_t)ty * t->w + tx];
            if (tint) {
                auto m = [&](int s) { return ((((c >> s) & 0xFF) * ((tint >> s) & 0xFF)) / 255) << s; };
                c = m(24) | m(16) | m(8) | (c & 0xFF);
            }
            blend(bx + x, by + y, c);
        }
    }
}

void HostCanvas::imageRotated(TexName tex, int sx, int sy, int sw, int sh, float px, float py,
                              float dx, float dy, float radians, uint32_t tint) {
    const HostTexture* t = get(tex);
    if (!t) return;
    float c = std::cos(radians), s = std::sin(radians);
    float r = std::sqrt((float)(sw * sw + sh * sh)) + 1;
    // walk every screen pixel near the pivot and sample the source backwards
    for (int y = (int)std::floor(dy - r); y <= (int)std::ceil(dy + r); y++)
        for (int x = (int)std::floor(dx - r); x <= (int)std::ceil(dx + r); x++) {
            float vx = x + 0.5f - dx, vy = y + 0.5f - dy;
            float ux = vx * c + vy * s + px, uy = -vx * s + vy * c + py;
            int tx = (int)std::floor(ux), ty = (int)std::floor(uy);
            if (tx < 0 || ty < 0 || tx >= sw || ty >= sh) continue;
            tx += sx; ty += sy;
            if (tx < 0 || ty < 0 || tx >= t->w || ty >= t->h) continue;
            uint32_t col = t->px[(size_t)ty * t->w + tx];
            if (tint) {
                auto m = [&](int sh2) { return ((((col >> sh2) & 0xFF) * ((tint >> sh2) & 0xFF)) / 255) << sh2; };
                col = m(24) | m(16) | m(8) | (col & 0xFF);
            }
            blend(x, y, col);
        }
}

void HostCanvas::imageStretch(TexName tex, int sx, int sy, int sw, int sh, float dx, float dy, float dw, float dh, uint32_t tint) {
    const HostTexture* t = get(tex);
    if (!t || sw <= 0 || sh <= 0) return;
    draws++;
    int bx = (int)std::floor(dx), by = (int)std::floor(dy), W = (int)dw, H = (int)dh;
    for (int y = 0; y < H; y++) {
        int ty = sy + y * sh / H;
        if (ty < 0 || ty >= t->h) continue;
        for (int x = 0; x < W; x++) {
            int tx = sx + x * sw / W;
            if (tx < 0 || tx >= t->w) continue;
            uint32_t c = t->px[(size_t)ty * t->w + tx];
            if (tint) {
                auto m = [&](int s) { return ((((c >> s) & 0xFF) * ((tint >> s) & 0xFF)) / 255) << s; };
                c = m(24) | m(16) | m(8) | (c & 0xFF);
            }
            blend(bx + x, by + y, c);
        }
    }
}

void HostCanvas::imageScaled(TexName tex, int sx, int sy, int sw, int sh, float dx, float dy,
                             float scale, bool flipX, uint32_t tint) {
    const HostTexture* t = get(tex);
    if (!t || scale <= 0) return;
    int bx = (int)std::floor(dx), by = (int)std::floor(dy);
    int W = (int)(sw * scale), H = (int)(sh * scale);
    for (int y = 0; y < H; y++) {
        int ty = sy + (int)(y / scale);
        if (ty < 0 || ty >= t->h) continue;
        for (int x = 0; x < W; x++) {
            int lx = (int)(x / scale);
            int tx = sx + (flipX ? sw - 1 - lx : lx);
            if (tx < 0 || tx >= t->w) continue;
            uint32_t c = t->px[(size_t)ty * t->w + tx];
            if (tint) {
                auto m = [&](int s) { return ((((c >> s) & 0xFF) * ((tint >> s) & 0xFF)) / 255) << s; };
                c = m(24) | m(16) | m(8) | (c & 0xFF);
            }
            blend(bx + x, by + y, c);
        }
    }
}

void HostCanvas::rect(float x, float y, float w, float h, uint32_t color) {
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    int x1 = (int)std::floor(x + w), y1 = (int)std::floor(y + h);
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++) blend(xx, yy, color);
}

void HostCanvas::line(float x1, float y1, float x2, float y2, uint32_t color, float) {
    int n = (int)std::ceil(std::max(std::fabs(x2 - x1), std::fabs(y2 - y1))) + 1;
    for (int i = 0; i <= n; i++) {
        float t = (float)i / n;
        blend((int)std::lround(x1 + (x2 - x1) * t), (int)std::lround(y1 + (y2 - y1) * t), color);
    }
}

float HostCanvas::textWidthPx(const std::string& s, float px) {
    if (!g_fontOk) return s.size() * px * 0.5f;
    float sc = stbtt_ScaleForPixelHeight(&g_font, px), w = 0;
    for (unsigned char ch : s) {
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&g_font, ch, &adv, &lsb);
        w += adv * sc;
    }
    return w;
}

void HostCanvas::text(float x, float y, float scale, uint32_t color, const std::string& s, int align) {
    if (scale < 0.41f && pixelTiny()) { pixelText(x, y, scale, color, s, align); return; }
    if (const BitmapFont* f = fontFor(scale)) { bitmapText(*f, x, y, scale, color, s, align); return; }
    if (!g_fontOk) return;
    float px = 30.0f * scale;   // 3DS system font is ~30px tall at scale 1
    float sc = stbtt_ScaleForPixelHeight(&g_font, px);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&g_font, &asc, &desc, &gap);
    float tw = textWidthPx(s, px);
    if (align == ALIGN_CENTER) x -= tw / 2;
    else if (align == ALIGN_RIGHT) x -= tw;
    float baseline = y + asc * sc;
    float pen = x;
    for (unsigned char chr : s) {
        int adv, lsb, gw, gh, gx, gy;
        stbtt_GetCodepointHMetrics(&g_font, chr, &adv, &lsb);
        unsigned char* bmp = stbtt_GetCodepointBitmapSubpixel(&g_font, sc, sc, pen - std::floor(pen), 0,
                                                              chr, &gw, &gh, &gx, &gy);
        for (int j = 0; j < gh; j++)
            for (int i = 0; i < gw; i++) {
                uint32_t a = bmp[j * gw + i] * (color & 0xFF) / 255;
                if (a) blend((int)std::floor(pen) + gx + i, (int)std::lround(baseline) + gy + j, (color & 0xFFFFFF00) | a);
            }
        stbtt_FreeBitmap(bmp, nullptr);
        pen += adv * sc;
    }
}

bool HostCanvas::writePng(const std::string& path, int scale) const {
    std::vector<uint8_t> out((size_t)W * scale * H * scale * 3);
    for (int y = 0; y < H * scale; y++)
        for (int x = 0; x < W * scale; x++) {
            uint32_t c = fb[(size_t)(y / scale) * W + x / scale];
            size_t i = ((size_t)y * W * scale + x) * 3;
            out[i] = c >> 24;
            out[i + 1] = (c >> 16) & 0xFF;
            out[i + 2] = (c >> 8) & 0xFF;
        }
    return stbi_write_png(path.c_str(), W * scale, H * scale, 3, out.data(), W * scale * 3) != 0;
}

void HostCanvas::cacheBegin(int) {
    if (inCache) return;
    const int S = CACHE_SIZE;
    savedFb.swap(fb);
    sW = W; sH = H; sox = ox; soy = oy; scx = cx; scy = cy; scw = cw; sch = ch;
    fb.assign((size_t)S * S, 0);
    W = H = S;
    ox = oy = cx = cy = 0;
    cw = ch = S;
    inCache = true;
}

void HostCanvas::cacheEnd(int which) {
    if (!inCache) return;
    cachePx[which].swap(fb);
    fb.swap(savedFb);
    W = sW; H = sH; ox = sox; oy = soy; cx = scx; cy = scy; cw = scw; ch = sch;
    inCache = false;
}

void HostCanvas::cacheDraw(int sx, int sy, int sw, int sh, float dx, float dy, int which) {
    draws++;
    const int S = CACHE_SIZE;
    if (cachePx[which].empty()) return;
    int bx = (int)std::floor(dx), by = (int)std::floor(dy);
    for (int y = 0; y < sh; y++) {
        int ty = cacheFlipY ? sy + sh - 1 - y : sy + y;
        if (ty < 0 || ty >= S) continue;
        for (int x = 0; x < sw; x++) {
            int tx = sx + x;
            if (tx < 0 || tx >= S) continue;
            blend(bx + x, by + y, cachePx[which][(size_t)ty * S + tx]);
        }
    }
}
