#include "audio/audio.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <dirent.h>

#include "core/vfs.h"

namespace audio {

static bool endsWith(const std::string& s, const char* e) {
    size_t n = strlen(e);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)s[s.size() - n + i]) != e[i]) return false;
    return true;
}

bool Engine::init(const std::string& root, Backend* backend) {
    root_ = root;
    backend_ = backend;
    // your XACT files: in audio/ (converter / setup), or the XACT folder
    // dropped in as-is
    std::string err;
    bool haveSoundBank = false;
    std::vector<std::string> seen;
    for (const char* sub : {"audio/", "XACT/", "Content/XACT/"}) {
        std::string dir = root + sub;
        DIR* d = opendir(dir.c_str());
        if (!d) continue;
        std::vector<std::string> xwb, xsb;
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name, low = n;
            for (char& ch : low) ch = (char)tolower((unsigned char)ch);
            bool dup = false;
            for (auto& s2 : seen) dup |= s2 == low;
            if (dup) continue;
            if (endsWith(n, ".xwb")) { xwb.push_back(n); seen.push_back(low); }
            else if (endsWith(n, ".xsb") && !haveSoundBank) { xsb.push_back(n); seen.push_back(low); }
        }
        closedir(d);
        for (auto& n : xwb) {
            WaveBank b;
            if (b.load(dir + n, err)) { log.push_back("wave bank " + b.name + ": " + std::to_string(b.entries.size())); banks_.push_back(std::move(b)); }
            else log.push_back(n + ": " + err);
        }
        for (auto& n : xsb) {
            if (sound_.load(dir + n, err)) { haveSoundBank = true; log.push_back("sound bank: " + std::to_string(sound_.cues.size()) + " cues"); }
            else log.push_back(n + ": " + err);
        }
    }
    if (!haveSoundBank) log.push_back("no sound bank (put your XACT folder in the stardew3ds folder)");
    // mod audio (Data/AudioChanges): id \t category \t looped \t path under the root
    std::vector<std::string> lines;
    if (sv::readTextLines(root + "data/audio.dat", lines)) {
        for (auto& l : lines) {
            std::vector<std::string> f;
            size_t s = 0;
            for (size_t i = 0; i <= l.size(); i++)
                if (i == l.size() || l[i] == '\t') { f.push_back(l.substr(s, i - s)); s = i + 1; }
            if (f.size() < 4) continue;
            Custom c;
            c.music = f[1] == "Music";
            c.loop = f[2] == "1";
            c.path = root + f[3];
            custom_[f[0]] = c;
        }
        log.push_back("mod audio: " + std::to_string(custom_.size()));
    }
    return ready();
}

bool Engine::hasCue(const std::string& cue) const { return custom_.count(cue) || sound_.cues.count(cue); }

bool Engine::resolve(const std::string& cue, std::string& bankPath, WaveEntry& e, bool& loop, float& vol) {
    auto it = sound_.cues.find(cue);
    if (it == sound_.cues.end() || it->second.tracks.empty()) return false;
    const Cue& c = it->second;
    int total = 0;
    for (auto& t : c.tracks) total += t.weight;
    rng_ = rng_ * 1103515245u + 12345u;
    int pick = (int)((rng_ >> 8) % (unsigned)(total > 0 ? total : 1));
    const CueTrack* t = &c.tracks[0];
    for (auto& tr : c.tracks) { if (pick < tr.weight) { t = &tr; break; } pick -= tr.weight; }
    if (t->bank < 0 || t->bank >= (int)sound_.waveBankNames.size()) return false;
    const std::string& want = sound_.waveBankNames[t->bank];
    for (auto& b : banks_) {
        if (b.name != want) continue;
        if (t->track < 0 || t->track >= (int)b.entries.size()) return false;
        bankPath = b.path;
        e = b.entries[t->track];
        loop = c.loop;
        vol = c.volume;
        return true;
    }
    return false;
}

// Decoded sound effects, by the wave they came from. A cue with several takes (footsteps, the hoe,
// the watering can) picks one each time: every take is decoded once and kept, instead of being read
// off the card and decoded again on each use.
static std::unordered_map<std::string, std::shared_ptr<Pcm>> g_waves;
static size_t g_waveBytes = 0;
static const size_t WAVE_CACHE_MAX = 3u << 20;

std::shared_ptr<Pcm> Engine::loadSfx(const std::string& cue, float& vol) {
    std::unique_ptr<Decoder> d;
    std::string waveKey;
    vol = 1;
    auto cu = custom_.find(cue);
    if (cu != custom_.end()) {
        d = openOgg(cu->second.path);
    } else {
        std::string path;
        WaveEntry e;
        bool loop;
        if (!resolve(cue, path, e, loop, vol)) return nullptr;
        waveKey = path + "#" + std::to_string(e.offset);
        auto have = g_waves.find(waveKey);
        if (have != g_waves.end()) return have->second;
        d = openWave(path, e);
    }
    if (!d) return nullptr;
    auto p = std::make_shared<Pcm>();
    p->channels = d->channels;
    p->rate = d->rate;
    int16_t buf[4096];
    size_t got;
    while ((got = d->read(buf, 4096 / p->channels)) > 0) {
        p->data.insert(p->data.end(), buf, buf + got * p->channels);
        if (p->data.size() > (size_t)p->rate * p->channels * 20) break;   // sanity: 20 s max
    }
    if (p->data.empty()) return nullptr;
    if (!waveKey.empty()) {
        if (g_waveBytes > WAVE_CACHE_MAX) {   // plenty kept: start again (anything still playing keeps its own copy)
            g_waves.clear();
            g_waveBytes = 0;
        }
        g_waves[waveKey] = p;
        g_waveBytes += p->data.size() * sizeof(int16_t);
    }
    return p;
}

void Engine::play(const std::string& cue, float volume) {
    if (!backend_ || !backend_->ok() || cue.empty()) return;
    auto it = cache_.find(cue);
    std::shared_ptr<Pcm> p;
    float vol = 1;
    if (it != cache_.end()) {
        p = it->second;
    } else {
        p = loadSfx(cue, vol);
        auto sc = sound_.cues.find(cue);
        bool variations = sc != sound_.cues.end() && sc->second.tracks.size() > 1;
        if (p && !variations) cache_[cue] = p;
        if (!p) { cache_[cue] = nullptr; return; }   // remember unknown cues
    }
    if (!p) return;
    auto sc = sound_.cues.find(cue);
    if (sc != sound_.cues.end()) vol = sc->second.volume;
    backend_->playSfx(p, vol * volume * sfxVolume);
}

void Engine::startTrack(const std::string& cue) {
    music_ = cue;
    if (cue.empty()) { backend_->stopMusic(); return; }
    std::unique_ptr<Decoder> d;
    bool loop = true;
    float vol = 1;
    auto cu = custom_.find(cue);
    if (cu != custom_.end()) {
        d = openOgg(cu->second.path);
        loop = cu->second.loop;
    } else {
        std::string path;
        WaveEntry e;
        if (resolve(cue, path, e, loop, vol)) d = openWave(path, e);
    }
    if (!d) { backend_->stopMusic(); return; }
    backend_->startMusic(std::move(d), loop, vol * musicVolume);
}

void Engine::music(const std::string& cue) {
    if (!backend_ || !backend_->ok()) { music_ = cue; return; }
    if (cue == (switching_ ? pending_ : music_)) return;
    if (music_.empty() || !backend_->musicPlaying()) { switching_ = false; startTrack(cue); return; }
    pending_ = cue;
    switching_ = true;
    fade_ = 1;
}

void Engine::update(float dt) {
    if (!backend_ || !backend_->ok()) return;
    if (switching_) {
        fade_ -= dt / 0.6f;   // fade the old track out over 0.6 s
        if (fade_ <= 0) {
            switching_ = false;
            startTrack(pending_);
        } else {
            backend_->setMusicVolume(fade_ * musicVolume);
        }
    }
    backend_->update();
}

}  // namespace audio
