#include "fonts.h"

#include <cstdlib>
#include <vector>

#include "core/vfs.h"

static bool loadFont(BitmapFont& f, const std::string& path, const std::string& tex) {
    std::vector<std::string> lines;
    if (!sv::readTextLines(path, lines) || lines.empty()) return false;
    f = BitmapFont();
    for (auto& l : lines) {
        if (l.rfind("#font\t", 0) == 0) {
            f.height = atoi(l.c_str() + 6);
            size_t t = l.find('\t', 6);
            f.line = t == std::string::npos ? f.height + 2 : atoi(l.c_str() + t + 1);
            continue;
        }
        int code = 0, x = 0, y = 0, w = 0;
        if (sscanf(l.c_str(), "%d\t%d\t%d\t%d", &code, &x, &y, &w) == 4 && code >= 0 && code < 256) {
            f.glyph[code].x = (int16_t)x;
            f.glyph[code].y = (int16_t)y;
            f.glyph[code].w = (int16_t)w;
        }
    }
    if (f.height <= 0) return false;
    f.tex = tex;
    return true;
}

bool loadUiFonts(Canvas& c, const std::function<std::string(const std::string&)>& resolve) {
    bool a = loadFont(c.fontSmall, resolve("data/font_small.dat"), "UiFont_small");
    bool b = loadFont(c.fontBold, resolve("data/font_bold.dat"), "LooseSprites_font_bold");
    if (a && c.texWidth("UiFont_small") == 0) c.fontSmall = BitmapFont();
    if (b && c.texWidth("LooseSprites_font_bold") == 0) c.fontBold = BitmapFont();
    return c.fontSmall.ok();
}
