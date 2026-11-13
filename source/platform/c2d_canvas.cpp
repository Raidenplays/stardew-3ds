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

// ---------------------------------------------------------------- map cache
bool C2DCanvas::cacheSupported() {
    if (!cacheTried) {
        cacheTried = true;
        const int S = CACHE_SIZE;
        for (int k = 0; k < 2; k++) {
            bool ok = C3D_TexInitVRAM(&cacheTex[k], S, S, GPU_RGBA8);
            if (!ok) ok = C3D_TexInit(&cacheTex[k], S, S, GPU_RGBA8);
            if (!ok) break;
            C3D_TexSetFilter(&cacheTex[k], GPU_NEAREST, GPU_NEAREST);
            cacheTarget[k] = C3D_RenderTargetCreateFromTex(&cacheTex[k], GPU_TEXFACE_2D, 0, -1);
            if (!cacheTarget[k]) { C3D_TexDelete(&cacheTex[k]); break; }
        }
        if (cacheTarget[0] && !cacheTarget[1]) {      // no room for the second: do without both, the map draws tile by tile
            C3D_RenderTargetDelete(cacheTarget[0]);
            C3D_TexDelete(&cacheTex[0]);
            cacheTarget[0] = nullptr;
        }
    }
    return cacheTarget[0] && cacheTarget[1] && screen;
}

void C2DCanvas::cacheBegin(int which) {
    if (!cacheTarget[which]) return;
    C2D_TargetClear(cacheTarget[which], 0);
    C2D_SceneBegin(cacheTarget[which]);
}

void C2DCanvas::cacheEnd(int which) {
    if (!cacheTarget[which] || !screen) return;
    C2D_SceneBegin(screen);
    cacheFresh[which] = true;
}

void C2DCanvas::cacheDraw(int sx, int sy, int sw, int sh, float dx, float dy, int which) {
    if (!cacheTarget[which]) return;
    draws++;
    if (cacheFresh[which]) {   // new contents at the same address: make citro2d bind it again
        C2D_Flush();
        C2D_Prepare();
        cacheFresh[which] = false;
    }
    const float S = (float)CACHE_SIZE;
    Tex3DS_SubTexture sub;
    sub.width = (u16)sw;
    sub.height = (u16)sh;
    sub.left = sx / S;
    sub.right = (sx + sw) / S;
    C2D_Image img = {&cacheTex[which], &sub};
    if (!cacheFlipY) {   // first stored row is the top, like our texture pages
        sub.top = 1.0f - sy / S;
        sub.bottom = 1.0f - (sy + sh) / S;
        C2D_DrawImageAt(img, dx, dy, 0.5f);
    } else {             // stored bottom-up: read it the other way and flip it back
        sub.top = (sy + sh) / S;
        sub.bottom = sy / S;
        C2D_DrawImageAt(img, dx, dy + sh, 0.5f, nullptr, 1.0f, -1.0f);
    }
}

int C2DCanvas::preload(TexName tex, int sx, int sy, int sw, int sh, int budget) {
    const Texture* t = get(tex);
    return t ? t->preload(sx, sy, sw, sh, budget) : 0;
}

void C2DCanvas::imageStretch(TexName tex, int sx, int sy, int sw, int sh, float dx, float dy, float dw, float dh, uint32_t tint) {
    draws++;
    const Texture* t = get(tex);
    if (!t || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    if (tint) {
        C2D_ImageTint it;
        C2D_PlainImageTint(&it, toC2D(tint), 1.0f);
        t->draw(sx, sy, sw, sh, dx, dy, false, &it, dw / sw, dh / sh);
    } else {
        t->draw(sx, sy, sw, sh, dx, dy, false, nullptr, dw / sw, dh / sh);
    }
}
