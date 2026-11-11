#include "audio/decoder.h"

#include <cstdio>
#include <cstring>
#include <vector>

#define STB_VORBIS_HEADER_ONLY
#include "third_party/stb_vorbis.c"

namespace audio {
namespace {

const int ADAPT[16] = {230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230};
const int COEF1[7] = {256, 512, 0, 192, 240, 460, 392};
const int COEF2[7] = {0, -256, 0, 64, 0, -208, -232};

inline int16_t clamp16(int v) { return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }

class WaveDecoder : public Decoder {
public:
    WaveDecoder(FILE* f, const WaveEntry& e) : f_(f), e_(e) {
        channels = e.channels;
        rate = e.rate;
        block_ = e.blockAlign();
        rewind();
    }
    ~WaveDecoder() override { if (f_) fclose(f_); }
    bool rewind() override {
        pos_ = 0;
        have_ = used_ = 0;
        return fseek(f_, e_.offset, SEEK_SET) == 0;
    }
    size_t read(int16_t* out, size_t frames) override {
        size_t done = 0;
        while (done < frames) {
            if (used_ >= have_ && !refill()) break;
            size_t n = std::min(frames - done, have_ - used_);
            memcpy(out + done * channels, pcm_.data() + used_ * channels, n * channels * sizeof(int16_t));
            used_ += n;
            done += n;
        }
        return done;
    }

private:
    bool refill() {
        if (pos_ >= e_.length) return false;
        used_ = 0;
        if (e_.codec == Codec::Adpcm) {
            if (pos_ + (uint32_t)block_ > e_.length) return false;
            raw_.resize(block_);
            if (fread(raw_.data(), 1, block_, f_) != (size_t)block_) return false;
            pos_ += block_;
            int fpb = msAdpcmFramesPerBlock(block_, channels);
            pcm_.resize((size_t)fpb * channels);
            have_ = decodeMsAdpcmBlock(raw_.data(), block_, channels, pcm_.data());
            return have_ > 0;
        }
        if (e_.codec != Codec::Pcm) return false;   // XMA / WMA aren't used on PC
        size_t chunk = std::min<uint32_t>(16384, e_.length - pos_);
        int bytesPerFrame = channels * (e_.bits == 16 ? 2 : 1);
        chunk -= chunk % bytesPerFrame;
        if (!chunk) return false;
        raw_.resize(chunk);
        if (fread(raw_.data(), 1, chunk, f_) != chunk) return false;
        pos_ += (uint32_t)chunk;
        have_ = chunk / bytesPerFrame;
        pcm_.resize(have_ * channels);
        if (e_.bits == 16) memcpy(pcm_.data(), raw_.data(), chunk);
        else for (size_t i = 0; i < chunk; i++) pcm_[i] = (int16_t)((raw_[i] - 128) << 8);
        return true;
    }
    FILE* f_;
    WaveEntry e_;
    int block_;
    uint32_t pos_ = 0;
    std::vector<uint8_t> raw_;
    std::vector<int16_t> pcm_;
    size_t have_ = 0, used_ = 0;
};

class OggDecoder : public Decoder {
public:
    explicit OggDecoder(stb_vorbis* v) : v_(v) {
        stb_vorbis_info i = stb_vorbis_get_info(v);
        channels = i.channels > 2 ? 2 : i.channels;
        rate = (int)i.sample_rate;
    }
    ~OggDecoder() override { stb_vorbis_close(v_); }
    size_t read(int16_t* out, size_t frames) override {
        return (size_t)stb_vorbis_get_samples_short_interleaved(v_, channels, out, (int)(frames * channels));
    }
    bool rewind() override { return stb_vorbis_seek_start(v_) != 0; }

private:
    stb_vorbis* v_;
};

}  // namespace

int decodeMsAdpcmBlock(const uint8_t* in, int blockAlign, int channels, int16_t* out) {
    if (channels < 1 || channels > 2 || blockAlign < 7 * channels) return 0;
    int pred[2], delta[2], s1[2], s2[2];
    const uint8_t* p = in;
    for (int c = 0; c < channels; c++) { pred[c] = *p++; if (pred[c] > 6) pred[c] = 0; }
    for (int c = 0; c < channels; c++) { delta[c] = (int16_t)(p[0] | (p[1] << 8)); p += 2; }
    for (int c = 0; c < channels; c++) { s1[c] = (int16_t)(p[0] | (p[1] << 8)); p += 2; }
    for (int c = 0; c < channels; c++) { s2[c] = (int16_t)(p[0] | (p[1] << 8)); p += 2; }
    int n = 0;
    for (int c = 0; c < channels; c++) out[n * channels + c] = (int16_t)s2[c];
    n++;
    for (int c = 0; c < channels; c++) out[n * channels + c] = (int16_t)s1[c];
    n++;
    const uint8_t* end = in + blockAlign;
    int ch = 0;
    int16_t* o = out + n * channels;
    while (p < end) {
        for (int half = 0; half < 2; half++) {
            int nib = half == 0 ? (*p >> 4) : (*p & 15);
            int signedNib = nib >= 8 ? nib - 16 : nib;
            int predicted = (s1[ch] * COEF1[pred[ch]] + s2[ch] * COEF2[pred[ch]]) / 256;
            int sample = clamp16(predicted + signedNib * delta[ch]);
            *o++ = (int16_t)sample;
            s2[ch] = s1[ch];
            s1[ch] = sample;
            delta[ch] = ADAPT[nib] * delta[ch] / 256;
            if (delta[ch] < 16) delta[ch] = 16;
            ch = (ch + 1) % channels;
        }
        p++;
    }
    return (int)((o - out) / channels);
}

std::unique_ptr<Decoder> openWave(const std::string& bankPath, const WaveEntry& e) {
    if (e.channels < 1 || e.channels > 2 || e.rate <= 0) return nullptr;
    if (e.codec != Codec::Pcm && e.codec != Codec::Adpcm) return nullptr;
    FILE* f = fopen(bankPath.c_str(), "rb");
    if (!f) return nullptr;
    return std::unique_ptr<Decoder>(new WaveDecoder(f, e));
}

std::unique_ptr<Decoder> openOgg(const std::string& path) {
    int err = 0;
    stb_vorbis* v = stb_vorbis_open_filename(path.c_str(), &err, nullptr);
    if (!v) return nullptr;
    return std::unique_ptr<Decoder>(new OggDecoder(v));
}

}  // namespace audio
