// Drawing interface shared by the 3DS (citro2d) and the PC screenshot tool.
// All game/UI drawing goes through this, so screenshots match the console.
#pragma once
#include <cstdint>
#include <string>

// 0xRRGGBBAA
constexpr uint32_t RGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 0xFF) {
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | a;
}

enum TextAlign : int { ALIGN_LEFT = 0, ALIGN_CENTER = 1, ALIGN_RIGHT = 2 };

// A pixel font drawn from a texture atlas, 1:1 (no scaling, so it stays crisp).
// The tables are written by the converter from your own game files.
struct BitmapFont {
    std::string tex;
    int height = 0, line = 0;
    struct Glyph { int16_t x = -1, y = 0, w = 0; };
    Glyph glyph[256];
    bool ok() const { return height > 0 && !tex.empty(); }
};

// UTF-8 -> Latin-1 code points (anything else becomes '?').
inline int nextCodepoint(const std::string& s, size_t& i) {
    unsigned char c = (unsigned char)s[i++];
    if (c < 0x80) return c;
    int extra = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 0;
    int cp = c & (0x3F >> extra);
    for (int k = 0; k < extra && i < s.size(); k++) cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
    return cp < 256 ? cp : '?';
}

class Canvas {
public:
    virtual ~Canvas() {}

    // Draw (sx,sy,sw,sh) of texture `tex` (a name under tex/) at (dx,dy).
    // tint != 0 multiplies the texture colour by it.
    virtual void image(const std::string& tex, int sx, int sy, int sw, int sh, float dx, float dy,
                       bool flipX = false, uint32_t tint = 0) = 0;
    virtual void rect(float x, float y, float w, float h, uint32_t color) = 0;
    virtual void line(float x1, float y1, float x2, float y2, uint32_t color, float width) = 0;
    // scale 0.5 is roughly a 15px-tall line, like the 3DS system font
    virtual void text(float x, float y, float scale, uint32_t color, const std::string& s,
                      int align = ALIGN_LEFT) = 0;
    virtual float textWidth(const std::string& s, float scale) = 0;
    virtual int texWidth(const std::string& tex) = 0;   // 0 if missing
    virtual void dropTextures() = 0;                    // free everything (map/season change)

    // Game fonts (set by loadUiFonts); backends use them when loaded.
    BitmapFont fontSmall, fontTiny, fontBold;
    const BitmapFont* fontFor(float scale) const {
        if (scale >= 0.55f && fontBold.ok()) return &fontBold;
        if (scale < 0.41f && fontTiny.ok()) return &fontTiny;
        if (fontSmall.ok()) return &fontSmall;
        return nullptr;
    }
    float bitmapWidth(const BitmapFont& f, const std::string& s) const {
        float w = 0;
        for (size_t i = 0; i < s.size();) {
            const BitmapFont::Glyph& g = f.glyph[nextCodepoint(s, i)];
            w += (g.x >= 0 ? g.w : f.glyph['?'].w) + (&f == &fontBold ? 1 : 0);
        }
        return w;
    }
    void bitmapText(const BitmapFont& f, float x, float y, float scale, uint32_t color, const std::string& s,
                    int align) {
        float w = bitmapWidth(f, s);
        if (align == ALIGN_CENTER) x -= w / 2;
        else if (align == ALIGN_RIGHT) x -= w;
        x = (float)(int)(x + 0.5f);
        // keep roughly where the old scaled font sat vertically
        float want = scale * 30.0f;
        y = (float)(int)(y + (want > f.height ? (want - f.height) / 2 : 0) + 0.5f);
        bool tint = &f != &fontBold;   // the bold font has its own colours
        for (size_t i = 0; i < s.size();) {
            int cp = nextCodepoint(s, i);
            const BitmapFont::Glyph* g = &f.glyph[cp];
            if (g->x < 0) g = &f.glyph['?'];
            if (g->x >= 0 && cp != ' ') image(f.tex, g->x, g->y, g->w, f.height, x, y, false, tint ? color : 0);
            x += g->w + (&f == &fontBold ? 1 : 0);
        }
    }

    // Stereo: horizontal shift in px per unit of depth for the eye being drawn.
    float eye = 0.0f;
    float shift(float depth) const { return depth * eye; }
};
