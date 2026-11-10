#include "setup/s3t_write.h"
#include <cstdio>

namespace s3t {
namespace {
constexpr int MAX_PAGE = 1024;

int nextPow2(int v) { int p = 8; while (p < v) p <<= 1; return p; }

void put16(std::vector<uint8_t>& o, int v) { o.push_back((uint8_t)v); o.push_back((uint8_t)(v >> 8)); }

// GPU 8x8 Morton tile index for pixel (x, gy), gy = row from the bottom.
inline uint32_t swz(int x, int gy, int pw) {
    uint32_t tile = (uint32_t)((gy >> 3) * (pw >> 3) + (x >> 3));
    int tx = x & 7, ty = gy & 7;
    uint32_t m = (tx & 1) | ((ty & 1) << 1) | ((tx & 2) << 1) | ((ty & 2) << 2) | ((tx & 4) << 2) | ((ty & 4) << 3);
    return (tile << 6) + m;
}
}  // namespace

bool encode(int w, int h, const std::vector<uint8_t>& rgba, std::vector<uint8_t>& out) {
    if (w <= 0 || h <= 0 || rgba.size() < (size_t)w * h * 4) return false;
    int pages = ((h + MAX_PAGE - 1) / MAX_PAGE) * ((w + MAX_PAGE - 1) / MAX_PAGE);
    out.clear();
    out.insert(out.end(), {'S', '3', 'T', '2'});
    put16(out, w); put16(out, h); put16(out, pages); put16(out, 0);
    for (int y0 = 0; y0 < h; y0 += MAX_PAGE) {
        for (int x0 = 0; x0 < w; x0 += MAX_PAGE) {
            int rw = w - x0 < MAX_PAGE ? w - x0 : MAX_PAGE;
            int rh = h - y0 < MAX_PAGE ? h - y0 : MAX_PAGE;
            int pw = nextPow2(rw), ph = nextPow2(rh);
            put16(out, x0); put16(out, y0); put16(out, rw); put16(out, rh); put16(out, pw); put16(out, ph);
            size_t base = out.size();
            out.resize(base + (size_t)pw * ph * 4, 0);
            uint8_t* d = &out[base];
            for (int y = 0; y < rh; y++) {
                int gy = ph - 1 - y;                       // GPU row 0 = bottom of the padded page
                const uint8_t* src = &rgba[((size_t)(y0 + y) * w + x0) * 4];
                for (int x = 0; x < rw; x++, src += 4) {
                    uint8_t* p = d + (size_t)swz(x, gy, pw) * 4;
                    p[0] = src[3]; p[1] = src[2]; p[2] = src[1]; p[3] = src[0];
                }
            }
        }
    }
    return true;
}

bool write(const std::string& path, int w, int h, const std::vector<uint8_t>& rgba) {
    std::vector<uint8_t> buf;
    if (!encode(w, h, rgba, buf)) return false;
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(buf.data(), 1, buf.size(), f) == buf.size();
    fclose(f);
    return ok;
}

}  // namespace s3t
