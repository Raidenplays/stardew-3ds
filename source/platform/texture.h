// Loads .s3t textures written by tools/convert.py and draws sub-rectangles
// of them with citro2d, in source-image pixel coordinates.
#pragma once
#include <3ds.h>
#include <citro2d.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct TexPage {
    C3D_Tex tex;
    int y0 = 0, h = 0, pw = 0, ph = 0;
};

class Texture {
public:
    ~Texture();
    bool load(const std::string& path);

    // Draw (sx,sy,sw,sh) from the source image at screen (dx,dy).
    void draw(int sx, int sy, int sw, int sh, float dx, float dy, bool flipX = false,
              const C2D_ImageTint* tint = nullptr, float scale = 1.0f) const;

    int width = 0, height = 0;

private:
    std::vector<TexPage> pages;
};

class TexCache {
public:
    explicit TexCache(const std::string& root) : root(root) {}
    const Texture* get(const std::string& name);   // nullptr if missing
    void clear();
    size_t count() const { return cache.size(); }

private:
    std::string root;
    std::unordered_map<std::string, std::unique_ptr<Texture>> cache;
};
