// citro2d implementation of Canvas for the 3DS.
#pragma once
#include <3ds.h>
#include <citro2d.h>

#include "render/canvas.h"
#include "texture.h"

class C2DCanvas : public Canvas {
public:
    const void* texLookup(TexName tex) override { return get(tex); }
    void imageTex(const void* handle, TexName tex, int sx, int sy, int sw, int sh, float dx, float dy) override {
        draws++;
        if (handle) static_cast<const Texture*>(handle)->draw(sx, sy, sw, sh, dx, dy, false, nullptr);
        (void)tex;
    }
    unsigned draws = 0;   // draw calls this frame (performance readout)
    C3D_RenderTarget* screen = nullptr;   // the target being drawn now (set before drawTop)
    bool cacheSupported() override;
    void cacheBegin(int which = 0) override;
    void cacheEnd(int which = 0) override;
    void cacheDraw(int sx, int sy, int sw, int sh, float dx, float dy, int which = 0) override;
    explicit C2DCanvas(std::function<std::string(const std::string&)> resolve);
    ~C2DCanvas();
    void beginFrame();   // clears the text buffer; call once per frame

    void image(TexName tex, int sx, int sy, int sw, int sh, float dx, float dy,
               bool flipX, uint32_t tint) override;
    void imageRotated(TexName tex, int sx, int sy, int sw, int sh, float px, float py, float dx,
                      float dy, float radians, uint32_t tint) override;
    void imageScaled(TexName tex, int sx, int sy, int sw, int sh, float dx, float dy, float scale,
                     bool flipX, uint32_t tint) override;
    void rect(float x, float y, float w, float h, uint32_t color) override;
    void line(float x1, float y1, float x2, float y2, uint32_t color, float width) override;
    void text(float x, float y, float scale, uint32_t color, const std::string& s, int align) override;
    float textWidth(const std::string& s, float scale) override;
    int texWidth(TexName tex) override;
    int texHeight(TexName tex) override;
    void dropTextures() override {
        cache.clear();
        for (auto& r : recent) { r.tex = nullptr; r.name.clear(); }
    }

private:
    TexCache cache;
    C3D_Tex cacheTex[2];
    C3D_RenderTarget* cacheTarget[2] = {nullptr, nullptr};
    bool cacheTried = false, cacheFresh[2] = {false, false};
    C2D_TextBuf textBuf;
    // small memo of recent names: tile layers ask for the same sheets over and over
    static constexpr int RECENT = 8;
    struct Recent { std::string name; const Texture* tex = nullptr; } recent[RECENT];
    int recentNext = 0;
    const Texture* get(TexName name);
};
