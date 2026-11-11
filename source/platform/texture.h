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
    int x0 = 0, y0 = 0, w = 0, h = 0, pw = 0, ph = 0;
    long fileOff = 0;        // where this page's pixels start in the .s3t
    u32 compLen = 0;         // S3T3: deflated size (0 = stored raw)
    bool loaded = false;     // pages load on first draw, and can be evicted
    u32 lastUsed = 0;        // frame number of the last draw
};

// Call once per frame (before drawing): pages drawn in an earlier frame can
// then be evicted when GPU memory runs out.
void texNewFrame();

class Texture {
public:
    ~Texture();
    bool load(const std::string& path);

    // Draw (sx,sy,sw,sh) from the source image at screen (dx,dy).
    // draw centred on (cx, cy), turned by radians around the centre
    void drawRotated(int sx, int sy, int sw, int sh, float cx, float cy, float radians,
                     const C2D_ImageTint* tint) const;
    void draw(int sx, int sy, int sw, int sh, float dx, float dy, bool flipX = false,
              const C2D_ImageTint* tint = nullptr, float scale = 1.0f) const;

    int width = 0, height = 0;

    // free every loaded page not drawn this frame, oldest first, until one
    // pw x ph RGBA8 page would fit; false if nothing more can go
    static bool evictOne();

private:
    bool ensure(TexPage& p) const;
    std::string path;
    bool packed = false, v2 = false;
    mutable std::vector<TexPage> pages;
};

#include <functional>

class TexCache {
public:
    // resolve("tex/Name.s3t") -> full path (lets mod layers override textures)
    explicit TexCache(std::function<std::string(const std::string&)> resolve) : resolve(resolve) {}
    const Texture* get(const std::string& name);   // nullptr if missing
    void clear();
    size_t count() const { return cache.size(); }

private:
    std::function<std::string(const std::string&)> resolve;
    std::unordered_map<std::string, std::unique_ptr<Texture>> cache;
};
