// Software Canvas for PC: draws into an RGBA buffer, writes PNG.
// Reads the same .s3t textures the 3DS uses (un-swizzling them).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "render/canvas.h"

struct HostTexture {
    int w = 0, h = 0;
    std::vector<uint32_t> px;   // 0xRRGGBBAA, row-major, top row first
};

class HostCanvas : public Canvas {
public:
    unsigned draws = 0;
    HostCanvas(const std::string& root, const std::string& fontPath);
    // optional: resolve "tex/Name.s3t" through mod layers
    std::string (*resolveFn)(const std::string& rel, void* ctx) = nullptr;
    void* resolveCtx = nullptr;
    void resize(int w, int h);
    void setOrigin(int x, int y) { ox = x; oy = y; }
    void setClip(int x, int y, int w, int h) { cx = x; cy = y; cw = w; ch = h; }

    void image(TexName tex, int sx, int sy, int sw, int sh, float dx, float dy,
               bool flipX, uint32_t tint) override;
    void imageRotated(TexName tex, int sx, int sy, int sw, int sh, float px, float py, float dx,
                      float dy, float radians, uint32_t tint) override;
    void imageScaled(TexName tex, int sx, int sy, int sw, int sh, float dx, float dy, float scale,
                     bool flipX, uint32_t tint) override;
    void rect(float x, float y, float w, float h, uint32_t color) override;
    void line(float x1, float y1, float x2, float y2, uint32_t color, float width) override;
    void text(float x, float y, float scale, uint32_t color, const std::string& s, int align) override;
    float textWidth(const std::string& s, float scale) override {
        if (scale < 0.41f && pixelTiny()) return pixelWidth(s);
        if (const BitmapFont* f = fontFor(scale)) return bitmapWidth(*f, s);
        return textWidthPx(s, 30.0f * scale);
    }
    int texWidth(TexName tex) override;
    int texHeight(TexName tex) override;
    void dropTextures() override { cache.clear(); }

    bool writePng(const std::string& path, int scale) const;
    int width() const { return W; }
    int height() const { return H; }
    std::vector<uint32_t> fb;

private:
    const HostTexture* get(TexName name);
    void blend(int x, int y, uint32_t c);
    float textWidthPx(const std::string& s, float px);

    std::string root;
    std::map<std::string, HostTexture> cache;
    std::vector<unsigned char> font;
    int W = 0, H = 0, ox = 0, oy = 0, cx = 0, cy = 0, cw = 0, ch = 0;
};
