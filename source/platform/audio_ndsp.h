// 3DS sound output through the DSP (ndsp). Needs the console's DSP firmware
// dumped to sdmc:/3ds/dspfirm.cdc (DSP1 does this once); without it the game
// just runs silently.
#pragma once
#include "audio/audio.h"

class NdspBackend : public audio::Backend {
public:
    NdspBackend();
    ~NdspBackend() override;
    bool ok() const override { return ok_; }
    void startMusic(std::unique_ptr<audio::Decoder> d, bool loop, float volume) override;
    void stopMusic() override;
    bool musicPlaying() const override { return music_ != nullptr; }
    void setMusicVolume(float v) override;
    void playSfx(const std::shared_ptr<audio::Pcm>& pcm, float volume) override;
    void update() override;

private:
    static const int MUSIC_CH = 0, SFX_FIRST = 1, SFX_COUNT = 7;
    static const int MUSIC_BUFS = 4, MUSIC_FRAMES = 4096;
    bool ok_ = false;
    std::unique_ptr<audio::Decoder> music_;
    bool loop_ = false, musicEnded_ = false;
    void* musicMem_ = nullptr;   // MUSIC_BUFS * MUSIC_FRAMES stereo PCM16, linear memory
    struct WaveBufSlot;
    WaveBufSlot* slots_ = nullptr;
    struct Sfx;
    Sfx* sfx_ = nullptr;
    int nextSfx_ = 0;
    bool fill(int i);
};
