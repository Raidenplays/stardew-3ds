// Host test for the on-console XNB reader + .s3t writer.
// Build: g++ -std=c++17 -O2 -Isource tests/xnb_test.cpp source/setup/xnb.cpp source/setup/lzx.cpp source/setup/s3t_write.cpp
// Run:   ./a.out SAMPLEDIR   (made by tests/make_xnb_samples.py)
#include "setup/s3t_write.h"
#include "setup/xnb.h"
#include <cstdio>
#include <fstream>
#include <iterator>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static std::vector<uint8_t> slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : "/tmp/xnbs";
    auto premul = slurp(dir + "/tex_premul.rgba");
    std::vector<uint8_t> first;
    for (const char* mode : {"raw", "lz4", "lzx"}) {
        xnb::File f;
        bool ok = xnb::load(dir + "/tex_" + mode + ".xnb", f);
        if (!ok) printf("%s: %s\n", mode, f.error.c_str());
        CHECK(ok);
        if (!ok) continue;
        int w, h;
        std::vector<uint8_t> px;
        CHECK(xnb::readTexture(f, w, h, px));
        CHECK(w == 96 && h == 400);
        // re-premultiply must give the original bytes back (within rounding)
        int bad = 0;
        for (size_t i = 0; i < px.size(); i += 4) {
            int a = px[i + 3];
            for (int c = 0; c < 3; c++) if (abs(px[i + c] * a / 255 - premul[i + c]) > 1) bad++;
            if (a != premul[i + 3]) bad++;
        }
        CHECK(bad == 0);
        if (first.empty()) first = px; else CHECK(px == first);
        printf("tex %s ok (%d bad)\n", mode, bad);
        if (std::string(mode) == "lzx") {
            CHECK(s3t::write(dir + "/tex.s3t", w, h, px));
            std::ofstream o(dir + "/tex_straight.rgba", std::ios::binary);
            o.write((const char*)px.data(), px.size());
        }
    }
    {
        xnb::File f;
        CHECK(xnb::load(dir + "/dict_lzx.xnb", f));
        std::vector<std::pair<std::string, std::string>> d;
        bool ok = xnb::readStringDict(f, d);
        if (!ok) printf("dict: %s\n", f.error.c_str());
        CHECK(ok && d.size() == 300);
        if (ok && d.size() == 300) {
            CHECK(d[0].first == "key0" && d[0].second == "value 0/");
            CHECK(d[299].first == "key299" && d[299].second == "value 299/" + std::string(299 % 40, 'x'));
        }
    }
    {
        xnb::File f;
        CHECK(xnb::load(dir + "/dict_int.xnb", f));
        std::vector<std::pair<std::string, std::string>> d;
        CHECK(xnb::readStringDict(f, d));
        CHECK(d.size() == 3 && d[1].first == "17" && d[2].first == "-2" && d[2].second == "neg");
    }
    printf(fails ? "%d FAILED\n" : "all xnb tests passed\n", fails);
    return fails ? 1 : 0;
}
