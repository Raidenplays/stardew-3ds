// Streams PCM16 out of a wave-bank entry (PCM or MS-ADPCM) or an .ogg file.
#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "audio/xact.h"

namespace audio {

class Decoder {
public:
    virtual ~Decoder() {}
    int channels = 0, rate = 0;
    // Fills up to `frames` frames of interleaved PCM16; returns frames written (0 = end).
    virtual size_t read(int16_t* out, size_t frames) = 0;
    virtual bool rewind() = 0;
};

std::unique_ptr<Decoder> openWave(const std::string& bankPath, const WaveEntry& e);
std::unique_ptr<Decoder> openOgg(const std::string& path);

// One MS-ADPCM block -> interleaved PCM16. Returns frames decoded.
int decodeMsAdpcmBlock(const uint8_t* in, int blockAlign, int channels, int16_t* out);
inline int msAdpcmFramesPerBlock(int blockAlign, int channels) { return (blockAlign - 7 * channels) * 2 / channels + 2; }

}  // namespace audio
