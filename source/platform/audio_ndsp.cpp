#include "platform/audio_ndsp.h"

#include <3ds.h>
#include <cstring>

struct NdspBackend::WaveBufSlot { ndspWaveBuf wb; };
struct NdspBackend::Sfx { ndspWaveBuf wb; void* mem = nullptr; };

NdspBackend::NdspBackend() {
    if (R_FAILED(ndspInit())) return;
    ok_ = true;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    musicMem_ = linearAlloc(MUSIC_BUFS * MUSIC_FRAMES * 2 * sizeof(int16_t));
    slots_ = new WaveBufSlot[MUSIC_BUFS]();
    sfx_ = new Sfx[SFX_COUNT]();
}

NdspBackend::~NdspBackend() {
    if (!ok_) return;
    for (int c = 0; c < 24; c++) ndspChnReset(c);
    for (int i = 0; i < SFX_COUNT; i++) if (sfx_[i].mem) linearFree(sfx_[i].mem);
    delete[] sfx_;
    delete[] slots_;
    if (musicMem_) linearFree(musicMem_);
    ndspExit();
}

static void setupChannel(int ch, int channels, int rate, float volume) {
    ndspChnReset(ch);
    ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
    ndspChnSetRate(ch, (float)rate);
    ndspChnSetFormat(ch, channels == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
    float mix[12] = {};
    mix[0] = mix[1] = volume;
    ndspChnSetMix(ch, mix);
}

// Decodes the next piece of music into buffer i and queues it. False at the end.
bool NdspBackend::fill(int i) {
    int16_t* buf = (int16_t*)musicMem_ + (size_t)i * MUSIC_FRAMES * 2;
    size_t got = 0;
    while (got < (size_t)MUSIC_FRAMES) {
        size_t n = music_->read(buf + got * music_->channels, MUSIC_FRAMES - got);
        if (n == 0) {
            if (!loop_ || !music_->rewind()) break;
            n = music_->read(buf + got * music_->channels, MUSIC_FRAMES - got);
            if (n == 0) break;
        }
        got += n;
    }
    if (got == 0) return false;
    ndspWaveBuf& wb = slots_[i].wb;
    memset(&wb, 0, sizeof wb);
    wb.data_vaddr = buf;
    wb.nsamples = (u32)got;
    DSP_FlushDataCache(buf, got * music_->channels * sizeof(int16_t));
    ndspChnWaveBufAdd(MUSIC_CH, &wb);
    return true;
}

void NdspBackend::startMusic(std::unique_ptr<audio::Decoder> d, bool loop, float volume) {
    if (!ok_) return;
    stopMusic();
    music_ = std::move(d);
    loop_ = loop;
    musicEnded_ = false;
    setupChannel(MUSIC_CH, music_->channels, music_->rate, volume);
    for (int i = 0; i < MUSIC_BUFS; i++)
        if (!fill(i)) { musicEnded_ = true; break; }
}

void NdspBackend::stopMusic() {
    if (!ok_) return;
    ndspChnWaveBufClear(MUSIC_CH);
    music_.reset();
}

void NdspBackend::setMusicVolume(float v) {
    if (!ok_) return;
    float mix[12] = {};
    mix[0] = mix[1] = v;
    ndspChnSetMix(MUSIC_CH, mix);
}

void NdspBackend::playSfx(const std::shared_ptr<audio::Pcm>& pcm, float volume) {
    if (!ok_ || !pcm || pcm->data.empty()) return;
    // a free channel, or else the oldest one
    int pick = -1;
    for (int k = 0; k < SFX_COUNT; k++) {
        int i = (nextSfx_ + k) % SFX_COUNT;
        if (!sfx_[i].mem || sfx_[i].wb.status == NDSP_WBUF_DONE) { pick = i; break; }
    }
    if (pick < 0) pick = nextSfx_;
    nextSfx_ = (pick + 1) % SFX_COUNT;
    Sfx& s = sfx_[pick];
    int ch = SFX_FIRST + pick;
    ndspChnWaveBufClear(ch);
    if (s.mem) linearFree(s.mem);
    size_t bytes = pcm->data.size() * sizeof(int16_t);
    s.mem = linearAlloc(bytes);
    if (!s.mem) return;
    memcpy(s.mem, pcm->data.data(), bytes);
    DSP_FlushDataCache(s.mem, bytes);
    setupChannel(ch, pcm->channels, pcm->rate, volume);
    memset(&s.wb, 0, sizeof s.wb);
    s.wb.data_vaddr = s.mem;
    s.wb.nsamples = (u32)(pcm->data.size() / pcm->channels);
    ndspChnWaveBufAdd(ch, &s.wb);
}

void NdspBackend::update() {
    if (!ok_) return;
    for (int i = 0; i < SFX_COUNT; i++)   // free finished effects
        if (sfx_[i].mem && sfx_[i].wb.status == NDSP_WBUF_DONE) { linearFree(sfx_[i].mem); sfx_[i].mem = nullptr; }
    if (!music_) return;
    bool anyQueued = false;
    for (int i = 0; i < MUSIC_BUFS; i++) {
        ndspWaveBuf& wb = slots_[i].wb;
        if (wb.status == NDSP_WBUF_DONE) {
            if (!musicEnded_ && !fill(i)) musicEnded_ = true;
        }
        if (wb.status == NDSP_WBUF_QUEUED || wb.status == NDSP_WBUF_PLAYING) anyQueued = true;
    }
    if (musicEnded_ && !anyQueued) music_.reset();   // a track that doesn't loop has finished
}
