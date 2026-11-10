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
    virtual int texWidth(const std::string& tex) = 0;   // 0 if missing
    virtual void dropTextures() = 0;                    // free everything (map/season change)

    // Stereo: horizontal shift in px per unit of depth for the eye being drawn.
    float eye = 0.0f;
    float shift(float depth) const { return depth * eye; }
};
