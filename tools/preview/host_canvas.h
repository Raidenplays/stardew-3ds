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
    HostCanvas(const std::string& root, const std::string& fontPath);
    void resize(int w, int h);
    void setOrigin(int x, int y) { ox = x; oy = y; }
    void setClip(int x, int y, int w, int h) { cx = x; cy = y; cw = w; ch = h; }

    void image(const std::string& tex, int sx, int sy, int sw, int sh, float dx, float dy,
               bool flipX, uint32_t tint) override;
    void rect(float x, float y, float w, float h, uint32_t color) override;
    void line(float x1, float y1, float x2, float y2, uint32_t color, float width) override;
    void text(float x, float y, float scale, uint32_t color, const std::string& s, int align) override;
    int texWidth(const std::string& tex) override;
    void dropTextures() override { cache.clear(); }

    bool writePng(const std::string& path, int scale) const;
    int width() const { return W; }
    int height() const { return H; }
    std::vector<uint32_t> fb;

private:
    const HostTexture* get(const std::string& name);
    void blend(int x, int y, uint32_t c);
    float textWidth(const std::string& s, float px);

    std::string root;
    std::map<std::string, HostTexture> cache;
    std::vector<unsigned char> font;
    int W = 0, H = 0, ox = 0, oy = 0, cx = 0, cy = 0, cw = 0, ch = 0;
};
