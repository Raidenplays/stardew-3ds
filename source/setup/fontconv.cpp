// Game fonts for the UI, same output as write_fonts() in tools/convert.py:
//   tex/UiFont_small.s3t + data/font_small.dat   (SmallFont baked to 16px)
//   data/font_bold.dat                           (glyph widths of font_bold)
#include "setup/fontconv.h"
#include "setup/s3t_write.h"
#include "setup/setup.h"
#include "setup/xnb.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace setup {
namespace {

struct Gray { int w = 0, h = 0; std::vector<float> a; float& at(int x, int y) { return a[y * w + x]; } };

// Resize like Pillow's BOX filter: whole source pixels whose centres fall in
// each output pixel's span, equal weights, horizontal pass first, 8-bit
// rounding between passes. (Matching it keeps the fonts identical to the PC
// converter's.)
static void boxTaps(int n, int nn, int i, std::vector<std::pair<int, float>>& taps) {
    double scale = (double)n / nn, fs = scale > 1 ? scale : 1, support = 0.5 * fs, ss = 1.0 / fs;
    double center = (i + 0.5) * scale;
    int xmin = (int)(center - support + 0.5), xmax = (int)(center + support + 0.5);
    if (xmin < 0) xmin = 0;
    if (xmax > n) xmax = n;
    taps.clear();
    double total = 0;
    for (int x = xmin; x < xmax; x++) {
        double v = (x - center + 0.5) * ss;
        if (v >= -0.5 && v < 0.5) { taps.push_back({x, 1}); total += 1; }
    }
    for (auto& t : taps) t.second = (float)(1.0 / total);
}

static float px8(float v) { v = std::floor(v + 0.5f); return v < 0 ? 0 : v > 255 ? 255 : v; }

Gray boxResize(const Gray& src, int nw, int nh) {
    std::vector<std::pair<int, float>> taps;
    Gray mid;
    mid.w = nw; mid.h = src.h; mid.a.assign((size_t)nw * src.h, 0);
    for (int x = 0; x < nw; x++) {
        boxTaps(src.w, nw, x, taps);
        for (int y = 0; y < src.h; y++) {
            float v = 0;
            for (auto& t : taps) v += src.a[y * src.w + t.first] * t.second;
            mid.at(x, y) = px8(v);
        }
    }
    Gray out;
    out.w = nw; out.h = nh; out.a.assign((size_t)nw * nh, 0);
    for (int y = 0; y < nh; y++) {
        boxTaps(src.h, nh, y, taps);
        for (int x = 0; x < nw; x++) {
            float v = 0;
            for (auto& t : taps) v += mid.a[t.first * nw + x] * t.second;
            out.at(x, y) = px8(v);
        }
    }
    return out;
}

bool writeText(const std::string& path, const std::string& text) {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return false;
    bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    fclose(f);
    return ok;
}

}  // namespace

bool convertSmallFont(const std::string& contentDir, const std::string& root, int px, std::string& err) {
    xnb::File f;
    if (!xnb::load(contentDir + "Fonts/SmallFont.xnb", f)) { err = f.error; return false; }
    xnb::SpriteFont sf;
    if (!xnb::readSpriteFont(f, sf)) { err = f.error; return false; }
    int line = sf.lineSpacing > 0 ? sf.lineSpacing : 33;
    float k = (float)px / line;
    std::map<uint32_t, Gray> cells;
    for (size_t i = 0; i < sf.chars.size(); i++) {
        uint32_t ch = sf.chars[i];
        if (ch > 255) continue;
        const xnb::Rect& b = sf.glyphs[i];
        const xnb::Rect& cr = sf.cropping[i];
        float lsb = sf.kerning[i * 3], wid = sf.kerning[i * 3 + 1], rsb = sf.kerning[i * 3 + 2];
        int cw = std::max(1, (int)std::lround(lsb + wid + rsb));
        Gray cell;
        cell.w = cw; cell.h = line; cell.a.assign((size_t)cw * line, 0);
        int ox = (int)lsb + cr.x, oy = cr.y;
        for (int y = 0; y < b.h; y++)
            for (int x = 0; x < b.w; x++) {
                int dx = ox + x, dy = oy + y, sx = b.x + x, sy = b.y + y;
                if (dx < 0 || dy < 0 || dx >= cw || dy >= line || sx >= sf.texW || sy >= sf.texH) continue;
                unsigned a = sf.rgba[((size_t)sy * sf.texW + sx) * 4 + 3];
                unsigned tmp = a * a + 128;              // pasted with itself as the mask (8-bit, like PIL)
                cell.at(dx, dy) = (float)((tmp + (tmp >> 8)) >> 8);
            }
        Gray sm = boxResize(cell, std::max(1, (int)std::lround(cw * k)), px);
        for (auto& v : sm.a) v = std::floor(v + 0.5f) > 110 ? 255.f : 0.f;   // 8-bit result, then threshold
        cells[ch] = std::move(sm);
    }
    if (cells.empty()) { err = "SmallFont has no glyphs"; return false; }
    int top = px, bot = 0;
    for (auto& kv : cells)
        for (int y = 0; y < kv.second.h; y++)
            for (int x = 0; x < kv.second.w; x++)
                if (kv.second.at(x, y) > 0) { top = std::min(top, y); bot = std::max(bot, y + 1); }
    if (top >= bot) { top = 0; bot = px; }
    int h = bot - top, cw = 0;
    for (auto& kv : cells) cw = std::max(cw, kv.second.w);
    const int cols = 16;
    int rows = ((int)cells.size() + cols - 1) / cols;
    int aw = cols * cw, ah = rows * h;
    std::vector<uint8_t> atlas((size_t)aw * ah * 4, 0);
    char buf[64];
    snprintf(buf, sizeof buf, "#font\t%d\t%d\n", h, std::max(h + 2, (int)std::lround(line * k) - 3));
    std::string dat = buf;
    int i = 0;
    for (auto& kv : cells) {
        int x0 = (i % cols) * cw, y0 = (i / cols) * h;
        Gray& g = kv.second;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < g.w; x++) {
                uint8_t* p = &atlas[((size_t)(y0 + y) * aw + x0 + x) * 4];
                p[0] = p[1] = p[2] = 255;
                p[3] = g.at(x, top + y) > 0 ? 255 : 0;
            }
        snprintf(buf, sizeof buf, "%u\t%d\t%d\t%d\n", (unsigned)kv.first, x0, y0, g.w);
        dat += buf;
        i++;
    }
    if (!s3t::write(root + "tex/UiFont_small.s3t", aw, ah, atlas)) { err = "couldn't write the font texture"; return false; }
    if (!writeText(root + "data/font_small.dat", dat)) { err = "couldn't write font_small.dat"; return false; }
    return true;
}

bool convertBoldFont(const std::string& contentDir, const std::string& root, std::string& err) {
    xnb::File f;
    if (!xnb::load(contentDir + "LooseSprites/font_bold.xnb", f)) { err = f.error; return false; }
    int w, h;
    std::vector<uint8_t> px;
    if (!xnb::readTexture(f, w, h, px)) { err = f.error; return false; }
    if (!s3t::write(root + "tex/LooseSprites_font_bold.s3t", w, h, px)) { err = "couldn't write font_bold"; return false; }
    int cols = w / 8;
    std::string dat = "#font\t16\t18\n";
    char buf[64];
    for (int code = 32; code < 256; code++) {
        int i = code - 32, x = (i % cols) * 8, y = (i / cols) * 16;
        if (y + 16 > h) break;
        int lo = 8, hi = -1;
        for (int yy = 0; yy < 16; yy++)
            for (int xx = 0; xx < 8; xx++)
                if (px[((size_t)(y + yy) * w + x + xx) * 4 + 3]) { lo = std::min(lo, xx); hi = std::max(hi, xx); }
        if (code == 32) snprintf(buf, sizeof buf, "%d\t%d\t%d\t4\n", code, x, y);
        else if (hi >= lo) snprintf(buf, sizeof buf, "%d\t%d\t%d\t%d\n", code, x + lo, y, hi - lo + 1);
        else continue;
        dat += buf;
    }
    if (!writeText(root + "data/font_bold.dat", dat)) { err = "couldn't write font_bold.dat"; return false; }
    return true;
}

}  // namespace setup
