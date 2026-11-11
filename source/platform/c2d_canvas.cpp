#include "c2d_canvas.h"

static inline u32 toC2D(uint32_t c) {
    return C2D_Color32((c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

C2DCanvas::C2DCanvas(std::function<std::string(const std::string&)> resolve) : cache(resolve) {
    textBuf = C2D_TextBufNew(4096);
}
C2DCanvas::~C2DCanvas() { C2D_TextBufDelete(textBuf); }

void C2DCanvas::beginFrame() { C2D_TextBufClear(textBuf); }

const Texture* C2DCanvas::get(TexName name) {
    // a few most-recent names: tile drawing alternates between sheets
    for (int i = 0; i < RECENT; i++)
        if (recent[i].tex && name == recent[i].name) return recent[i].tex;
    std::string s = name.str();
    const Texture* t = cache.get(s);
    if (t) {
        recentNext = (recentNext + 1) % RECENT;
        recent[recentNext].name = s;
        recent[recentNext].tex = t;
    }
    return t;
}

void C2DCanvas::image(TexName tex, int sx, int sy, int sw, int sh, float dx, float dy,
                      bool flipX, uint32_t tint) {
    draws++;
    const Texture* t = get(tex);
    if (!t) return;
    if (tint) {
        C2D_ImageTint it;
        C2D_PlainImageTint(&it, toC2D(tint), 1.0f);
        t->draw(sx, sy, sw, sh, dx, dy, flipX, &it);
    } else {
        t->draw(sx, sy, sw, sh, dx, dy, flipX, nullptr);
    }
}

void C2DCanvas::imageRotated(TexName tex, int sx, int sy, int sw, int sh, float px, float py,
                             float dx, float dy, float radians, uint32_t tint) {
    draws++;
    const Texture* t = get(tex);
    if (!t) return;
    // citro2d turns around the image centre: move the centre so the pivot stays put
    float ox = sw * 0.5f - px, oy = sh * 0.5f - py;
    float c = cosf(radians), s = sinf(radians);
    float cx = dx + ox * c - oy * s, cy = dy + ox * s + oy * c;
    if (tint) {
        C2D_ImageTint it;
        C2D_PlainImageTint(&it, toC2D(tint), 1.0f);
        t->drawRotated(sx, sy, sw, sh, cx, cy, radians, &it);
    } else {
        t->drawRotated(sx, sy, sw, sh, cx, cy, radians, nullptr);
    }
}

void C2DCanvas::imageScaled(TexName tex, int sx, int sy, int sw, int sh, float dx, float dy,
                            float scale, bool flipX, uint32_t tint) {
    draws++;
    const Texture* t = get(tex);
    if (!t) return;
    if (tint) {
        C2D_ImageTint it;
        C2D_PlainImageTint(&it, toC2D(tint), 1.0f);
        t->draw(sx, sy, sw, sh, dx, dy, flipX, &it, scale);
    } else {
        t->draw(sx, sy, sw, sh, dx, dy, flipX, nullptr, scale);
    }
}

void C2DCanvas::rect(float x, float y, float w, float h, uint32_t color) {
    draws++;
    C2D_DrawRectSolid(x, y, 0.5f, w, h, toC2D(color));
}

void C2DCanvas::line(float x1, float y1, float x2, float y2, uint32_t color, float width) {
    u32 c = toC2D(color);
    C2D_DrawLine(x1, y1, c, x2, y2, c, width, 0.5f);
}

void C2DCanvas::text(float x, float y, float scale, uint32_t color, const std::string& s, int align) {
    if (scale < 0.41f && pixelTiny()) { pixelText(x, y, scale, color, s, align); return; }
    if (const BitmapFont* f = fontFor(scale)) { bitmapText(*f, x, y, scale, color, s, align); return; }
    C2D_Text t;
    C2D_TextParse(&t, textBuf, s.c_str());
    C2D_TextOptimize(&t);
    u32 flags = C2D_WithColor | (align == ALIGN_CENTER ? C2D_AlignCenter : align == ALIGN_RIGHT ? C2D_AlignRight : 0);
    C2D_DrawText(&t, flags, x, y, 0.5f, scale, scale, toC2D(color));
}

int C2DCanvas::texWidth(TexName tex) {
    const Texture* t = get(tex);
    return t ? t->width : 0;
}

int C2DCanvas::texHeight(TexName tex) {
    const Texture* t = get(tex);
    return t ? t->height : 0;
}

float C2DCanvas::textWidth(const std::string& s, float scale) {
    if (scale < 0.41f && pixelTiny()) return pixelWidth(s);
    if (const BitmapFont* f = fontFor(scale)) return bitmapWidth(*f, s);
    C2D_Text t;
    C2D_TextParse(&t, textBuf, s.c_str());
    float w = 0, h = 0;
    C2D_TextGetDimensions(&t, scale, scale, &w, &h);
    return w;
}
