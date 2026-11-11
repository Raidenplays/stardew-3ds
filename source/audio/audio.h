// The game's sound: music and sound effects by cue name, played from the
// player's own XACT files (copied to sdmc:/3ds/stardew3ds/audio/) and from
// mod music (.ogg) listed in data/audio.dat. The platform supplies a Backend.
#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "audio/decoder.h"
#include "audio/xact.h"

namespace audio {

struct Pcm {
    int channels = 0, rate = 0;
    std::vector<int16_t> data;   // interleaved
};

class Backend {
public:
    virtual ~Backend() {}
    virtual bool ok() const = 0;
    // music: one stream; the backend pulls from the decoder as it plays
    virtual void startMusic(std::unique_ptr<Decoder> d, bool loop, float volume) = 0;
    virtual void stopMusic() = 0;
    virtual bool musicPlaying() const = 0;
    virtual void setMusicVolume(float v) = 0;
    virtual void playSfx(const std::shared_ptr<Pcm>& pcm, float volume) = 0;
    virtual void update() = 0;   // call every frame
};

class Engine {
public:
    // root ends in '/'. Missing files just mean silence.
    bool init(const std::string& root, Backend* backend);
    void play(const std::string& cue, float volume = 1);
    // Changes the music; "" fades out. Asking for the current track does nothing.
    void music(const std::string& cue);
    const std::string& currentMusic() const { return music_; }
    void update(float dt);
    bool hasCue(const std::string& cue) const;
    bool ready() const { return backend_ && backend_->ok() && (!sound_.cues.empty() || !custom_.empty()); }
    float musicVolume = 0.75f, sfxVolume = 0.9f;
    std::vector<std::string> log;   // what got loaded / what failed

    // Picks the wave a cue plays (random by weight). False if unknown.
    bool resolve(const std::string& cue, std::string& bankPath, WaveEntry& e, bool& loop, float& vol);

private:
    struct Custom { std::string path; bool loop = false; bool music = false; };
    std::shared_ptr<Pcm> loadSfx(const std::string& cue, float& vol);
    void startTrack(const std::string& cue);

    Backend* backend_ = nullptr;
    std::string root_;
    std::vector<WaveBank> banks_;
    SoundBank sound_;
    std::unordered_map<std::string, Custom> custom_;
    std::unordered_map<std::string, std::shared_ptr<Pcm>> cache_;
    std::string music_, pending_;
    float fade_ = 0;          // >0: fading the current music out
    bool switching_ = false;
    unsigned rng_ = 12345;
};

}  // namespace audio
