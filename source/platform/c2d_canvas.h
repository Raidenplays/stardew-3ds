// citro2d implementation of Canvas for the 3DS.
#pragma once
#include <3ds.h>
#include <citro2d.h>

#include "render/canvas.h"
#include "texture.h"

class C2DCanvas : public Canvas {
public:
    explicit C2DCanvas(std::function<std::string(const std::string&)> resolve);
    ~C2DCanvas();
    void beginFrame();   // clears the text buffer; call once per frame

    void image(const std::string& tex, int sx, int sy, int sw, int sh, float dx, float dy,
               bool flipX, uint32_t tint) override;
    void imageRotated(const std::string& tex, int sx, int sy, int sw, int sh, float px, float py, float dx,
                      float dy, float radians, uint32_t tint) override;
    void rect(float x, float y, float w, float h, uint32_t color) override;
    void line(float x1, float y1, float x2, float y2, uint32_t color, float width) override;
    void text(float x, float y, float scale, uint32_t color, const std::string& s, int align) override;
    float textWidth(const std::string& s, float scale) override;
    int texWidth(const std::string& tex) override;
    void dropTextures() override {
        cache.clear();
        lastTex = nullptr;
        lastName.clear();
    }

private:
    TexCache cache;
    C2D_TextBuf textBuf;
    // tiny one-entry memo: tile layers ask for the same sheet over and over
    std::string lastName;
    const Texture* lastTex = nullptr;
    const Texture* get(const std::string& name);
};
