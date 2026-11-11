// Host test for the XACT reader, decoders and audio engine.
// Build: see .github/workflows/build.yml (host-tests)
// Run:   ./audio_test SAMPLEDIR [some.ogg]
#include "audio/audio.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static std::vector<int16_t> raw(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::vector<char> b((std::istreambuf_iterator<char>(f)), {});
    std::vector<int16_t> s(b.size() / 2);
    memcpy(s.data(), b.data(), s.size() * 2);
    return s;
}

static std::vector<int16_t> drain(audio::Decoder& d) {
    std::vector<int16_t> out;
    int16_t buf[1000];
    size_t n;
    while ((n = d.read(buf, 1000 / d.channels)) > 0) out.insert(out.end(), buf, buf + n * d.channels);
    return out;
}

// records what the engine asks for
struct FakeBackend : audio::Backend {
    std::unique_ptr<audio::Decoder> music;
    bool loop = false;
    float vol = 0;
    int sfx = 0;
    bool ok() const override { return true; }
    void startMusic(std::unique_ptr<audio::Decoder> d, bool l, float v) override { music = std::move(d); loop = l; vol = v; }
    void stopMusic() override { music.reset(); }
    bool musicPlaying() const override { return music != nullptr; }
    void setMusicVolume(float v) override { vol = v; }
    void playSfx(const std::shared_ptr<audio::Pcm>&, float) override { sfx++; }
    void update() override {}
};

int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : "/tmp/xact";
    std::string err;
    audio::WaveBank wb;
    CHECK(wb.load(dir + "/test.xwb", err));
    CHECK(wb.name == "Test Bank" && wb.entries.size() == 3);
    if (wb.entries.size() == 3) {
        CHECK(wb.entries[0].rate == 22050 && wb.entries[0].channels == 1 && wb.entries[0].bits == 16);
        CHECK(wb.entries[1].codec == audio::Codec::Adpcm && wb.entries[1].channels == 2 && wb.entries[1].blockAlign() == 280);
        CHECK(wb.entries[2].bits == 8 && wb.entries[2].rate == 11025);
        for (int i = 0; i < 3; i++) {
            auto d = audio::openWave(wb.path, wb.entries[i]);
            CHECK(d != nullptr);
            if (!d) continue;
            auto got = drain(*d);
            auto want = raw(dir + "/w" + std::to_string(i) + ".raw");
            CHECK(got == want);
            printf("wave %d: %zu samples %s\n", i, got.size(), got == want ? "exact" : "MISMATCH");
            if (i == 1) {   // and the ADPCM is a faithful copy of the tone
                auto src = raw(dir + "/w1src.raw");
                double se = 0, sn = 0;
                for (size_t k = 0; k < std::min(src.size(), got.size()); k++) { se += std::pow(src[k] - got[k], 2); sn += std::pow(src[k], 2); }
                double snr = 10 * std::log10(sn / (se + 1));
                printf("  adpcm snr %.1f dB\n", snr);
                CHECK(snr > 20);
                CHECK(d->rewind() && drain(*d) == got);
            }
        }
    }
    audio::SoundBank sb;
    CHECK(sb.load(dir + "/test.xsb", err));
    CHECK(sb.waveBankNames.size() == 1 && sb.waveBankNames[0] == "Test Bank");
    CHECK(sb.cues.size() == 3);
    CHECK(sb.cues["beep"].tracks.size() == 1 && sb.cues["beep"].tracks[0].track == 0 && !sb.cues["beep"].loop);
    CHECK(sb.cues["song"].tracks.size() == 1 && sb.cues["song"].tracks[0].track == 1 && sb.cues["song"].loop);
    CHECK(sb.cues["rand"].tracks.size() == 2 && sb.cues["rand"].tracks[1].track == 2);
    CHECK(std::fabs(audio::xactVolume(0xB4) - 1.0f) < 0.05f);

    // the engine over a fake backend
    std::string root = dir + "/root/";
    FakeBackend fb;
    audio::Engine eng;
    eng.init(root, &fb);
    for (auto& l : eng.log) printf("  log: %s\n", l.c_str());
    CHECK(eng.hasCue("beep") && eng.hasCue("song"));
    eng.play("beep");
    eng.play("nope");
    CHECK(fb.sfx == 1);
    eng.music("song");
    CHECK(fb.music && fb.loop && eng.currentMusic() == "song");
    eng.music("song");   // same track: no restart
    if (argc > 2) {
        CHECK(eng.hasCue("modsong"));
        eng.music("modsong");   // fades out first
        for (int i = 0; i < 60; i++) eng.update(1 / 60.0f);
        CHECK(eng.currentMusic() == "modsong" && fb.music && fb.music->rate > 0);
        if (fb.music) {
            int16_t buf[4096];
            size_t n = fb.music->read(buf, 2048);
            printf("ogg: %d ch %d Hz, read %zu frames\n", fb.music->channels, fb.music->rate, n);
            CHECK(n == 2048);
        }
    }
    eng.music("");
    for (int i = 0; i < 60; i++) eng.update(1 / 60.0f);
    CHECK(!fb.music);
    printf(fails ? "%d FAILED\n" : "all audio tests passed\n", fails);
    return fails ? 1 : 0;
}
