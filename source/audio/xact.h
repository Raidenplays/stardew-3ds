// Reads Stardew Valley's XACT audio files (Content/XACT/*.xwb, *.xsb) straight
// from the SD card: wave banks hold the sound data, the sound bank maps cue
// names ("hoeHit", "spring1", ...) to waves. Written from the publicly
// documented layout (see MonoGame's XACT reader); no conversion needed.
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace audio {

enum class Codec { Pcm, Xma, Adpcm, Wma };

struct WaveEntry {
    Codec codec = Codec::Pcm;
    int channels = 0, rate = 0, align = 0, bits = 16;   // align: raw MINIWAVEFORMAT field
    uint32_t offset = 0, length = 0;                    // absolute file offset, bytes
    uint32_t loopStart = 0, loopLength = 0;             // in samples (0 = none)
    int blockAlign() const;                             // bytes per ADPCM block (all channels)
};

struct WaveBank {
    std::string path, name;
    std::vector<WaveEntry> entries;
    bool load(const std::string& path, std::string& err);
};

struct CueTrack { int bank = 0, track = 0; int weight = 1; };

struct Cue {
    std::vector<CueTrack> tracks;   // one is picked at random (by weight) each play
    bool loop = false;              // loop count 255 = forever (music)
    float volume = 1;
    int category = 0;
};

struct SoundBank {
    std::vector<std::string> waveBankNames;   // index -> bank name ("Wave Bank", "Wave Bank(1.4)")
    std::unordered_map<std::string, Cue> cues;
    bool load(const std::string& path, std::string& err);
};

// XACT's packed volume byte -> linear gain
float xactVolume(uint8_t b);

}  // namespace audio
