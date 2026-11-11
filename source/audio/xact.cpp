#include "audio/xact.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace audio {
namespace {

struct Reader {
    const std::vector<uint8_t>& d;
    size_t pos = 0;
    bool bad = false;
    explicit Reader(const std::vector<uint8_t>& data) : d(data) {}
    void seek(size_t p) { pos = p; if (p > d.size()) bad = true; }
    void skip(size_t n) { seek(pos + n); }
    uint8_t u8() { if (pos >= d.size()) { bad = true; return 0; } return d[pos++]; }
    uint16_t u16() { uint16_t a = u8(); return (uint16_t)(a | (u8() << 8)); }
    uint32_t u32() { uint32_t a = u16(); return a | ((uint32_t)u16() << 16); }
    int16_t i16() { return (int16_t)u16(); }
    std::string fixed(size_t n) {
        std::string s;
        for (size_t i = 0; i < n; i++) { char c = (char)u8(); if (c) s += c; else { skip(n - i - 1); break; } }
        return s;
    }
};

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0);
    bool ok = n > 0 && fread(out.data(), 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    return ok;
}

// The parts of a sound we use: which waves it plays and whether it loops.
void readSound(Reader& r, size_t at, Cue& cue) {
    size_t save = r.pos;
    r.seek(at);
    uint8_t flags = r.u8();
    bool complex = flags & 1;
    cue.category = r.u16();
    cue.volume = xactVolume(r.u8());
    r.i16();   // pitch
    r.u8();    // priority
    r.u16();   // entry length / filter
    int numClips = 0;
    if (complex) {
        numClips = r.u8();
    } else {
        CueTrack t;
        t.track = r.u16();
        t.bank = r.u8();
        cue.tracks.push_back(t);
    }
    if (flags & 0x0E) {   // RPC curves: length-prefixed, skip
        size_t cur = r.pos;
        uint16_t len = r.u16();
        r.seek(cur + len);
    }
    if (flags & 0x10) r.skip(7);   // DSP (reverb) preset
    for (int c = 0; c < numClips && !r.bad; c++) {
        r.u8();   // clip volume
        uint32_t clipOffset = r.u32();
        r.u16();
        r.u16();
        size_t back = r.pos;
        r.seek(clipOffset);
        int numEvents = r.u8();
        for (int e = 0; e < numEvents && !r.bad; e++) {
            uint32_t info = r.u32();
            r.u16();   // random offset
            int id = info & 0x1F;
            if (id == 1 || id == 4) {   // play one wave (4 = with variation ranges)
                r.u8();
                r.u8();   // flags
                CueTrack t;
                t.track = r.u16();
                t.bank = r.u8();
                int loops = r.u8();
                r.u16();
                r.u16();   // pan
                if (id == 4) r.skip(2 + 2 + 1 + 1 + 16 + 1 + 1);
                if (loops == 255) cue.loop = true;
                cue.tracks.push_back(t);
            } else if (id == 3 || id == 6) {   // play one of several waves
                r.u8();
                r.u8();
                int loops = r.u8();
                r.u16();
                r.u16();
                if (id == 6) r.skip(2 + 2 + 1 + 1 + 16 + 1 + 1);
                int n = r.u16();
                r.u8();
                r.skip(5);
                for (int j = 0; j < n && !r.bad; j++) {
                    CueTrack t;
                    t.track = r.u16();
                    t.bank = r.u8();
                    int lo = r.u8(), hi = r.u8();
                    t.weight = std::max(1, hi - lo);
                    cue.tracks.push_back(t);
                }
                if (loops == 255) cue.loop = true;
            } else if (id == 8) {   // volume event: fixed size, nothing we need
                r.skip(2 + 1 + 4 + 9);
            } else {
                break;   // unknown event size: stop reading this clip
            }
        }
        r.seek(back);
    }
    r.seek(save);
}

}  // namespace

float xactVolume(uint8_t b) {
    const double a = -96.0, bb = 0.432254984608615, c = 80.1748600297963, d = 67.7385212334047;
    double db = ((a - d) / (1 + std::pow(b / c, bb))) + d;
    return (float)std::pow(10.0, db / 20.0);
}

int WaveEntry::blockAlign() const {
    if (codec == Codec::Adpcm) return (align + 22) * channels;
    return channels * (bits == 16 ? 2 : 1);
}

bool WaveBank::load(const std::string& p, std::string& err) {
    path = p;
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) { err = "can't open " + p; return false; }
    std::vector<uint8_t> head(4096);
    size_t got = fread(head.data(), 1, head.size(), f);
    head.resize(got);
    Reader r(head);
    if (r.fixed(4) != "WBND") { fclose(f); err = "not a wave bank"; return false; }
    int version = (int)r.u32();
    int last = version <= 3 ? 3 : 4;
    if (version >= 42) r.u32();
    uint32_t segOff[5] = {}, segLen[5] = {};
    for (int i = 0; i <= last; i++) { segOff[i] = r.u32(); segLen[i] = r.u32(); }
    r.seek(segOff[0]);
    uint32_t flags = r.u32();
    uint32_t count = r.u32();
    name = r.fixed(version == 2 || version == 3 ? 16 : 64);
    uint32_t metaSize = r.u32();
    r.u32();   // name element size
    uint32_t alignment = r.u32();
    uint32_t compactFormat = (flags & 0x00020000) ? r.u32() : 0;
    if (r.bad || count > 100000) { fclose(f); err = "bad wave bank header"; return false; }
    uint32_t play = segOff[last];
    // entry table
    std::vector<uint8_t> meta((size_t)count * (metaSize ? metaSize : 4));
    fseek(f, segOff[1], SEEK_SET);
    if (fread(meta.data(), 1, meta.size(), f) != meta.size()) { fclose(f); err = "truncated wave bank"; return false; }
    fclose(f);
    Reader m(meta);
    entries.assign(count, WaveEntry());
    auto decode = [&](WaveEntry& e, uint32_t fmt) {
        if (version == 1) {
            e.codec = (Codec)(fmt & 1);
            e.channels = (fmt >> 1) & 7;
            e.rate = (fmt >> 5) & 0x3FFFF;
            e.align = (fmt >> 23) & 0xFF;
            e.bits = ((fmt >> 31) & 1) ? 16 : 8;
        } else {
            e.codec = (Codec)(fmt & 3);
            e.channels = (fmt >> 2) & 7;
            e.rate = (fmt >> 5) & 0x3FFFF;
            e.align = (fmt >> 23) & 0xFF;
            e.bits = ((fmt >> 31) & 1) ? 16 : 8;
        }
    };
    if (flags & 0x00020000) {   // compact: offsets only
        std::vector<uint32_t> offs(count);
        for (uint32_t i = 0; i < count; i++) offs[i] = (m.u32() & ((1u << 21) - 1)) * alignment;
        for (uint32_t i = 0; i < count; i++) {
            WaveEntry& e = entries[i];
            decode(e, compactFormat);
            uint32_t next = i + 1 < count ? offs[i + 1] : segLen[last];
            e.offset = play + offs[i];
            e.length = next - offs[i];
        }
    } else {
        for (uint32_t i = 0; i < count; i++) {
            WaveEntry& e = entries[i];
            size_t base = (size_t)i * metaSize;
            m.seek(base);
            uint32_t fmt = 0, off = 0, len = 0;
            if (version == 1) {
                fmt = m.u32(); off = m.u32(); len = m.u32(); e.loopStart = m.u32(); e.loopLength = m.u32();
            } else {
                m.u32();   // flags + duration
                if (metaSize >= 8) fmt = m.u32();
                if (metaSize >= 12) off = m.u32();
                if (metaSize >= 16) len = m.u32();
                if (metaSize >= 20) e.loopStart = m.u32();
                if (metaSize >= 24) e.loopLength = m.u32();
                if (metaSize < 24 && len) len = segLen[last];
            }
            decode(e, fmt);
            e.offset = play + off;
            e.length = len;
        }
    }
    return true;
}

bool SoundBank::load(const std::string& path, std::string& err) {
    std::vector<uint8_t> d;
    if (!readFile(path, d)) { err = "can't open " + path; return false; }
    Reader r(d);
    if (r.fixed(4) != "SDBK") { err = "not a sound bank"; return false; }
    r.u16();   // tool version
    r.u16();   // format version
    r.u16();   // crc
    r.u32(); r.u32();   // last modified
    r.u8();    // platform
    int numSimple = r.u16(), numComplex = r.u16();
    r.u16();
    r.u16();   // total cues
    int numBanks = r.u8();
    r.u16();   // sounds
    int nameLen = r.u16();
    r.u16();
    uint32_t simpleOff = r.u32(), complexOff = r.u32(), namesOff = r.u32();
    r.u32(); r.u32(); r.u32();
    uint32_t bankNamesOff = r.u32();
    if (r.bad) { err = "bad sound bank header"; return false; }
    r.seek(bankNamesOff);
    for (int i = 0; i < numBanks; i++) waveBankNames.push_back(r.fixed(64));
    std::vector<std::string> names;
    r.seek(namesOff);
    std::string cur;
    for (int i = 0; i < nameLen; i++) {
        char c = (char)r.u8();
        if (c) cur += c; else { names.push_back(cur); cur.clear(); }
    }
    auto nameOf = [&](int i) { return i < (int)names.size() ? names[i] : std::string(); };
    r.seek(simpleOff);
    for (int i = 0; i < numSimple && !r.bad; i++) {
        r.u8();
        uint32_t soundOff = r.u32();
        Cue cue;
        readSound(r, soundOff, cue);
        cues[nameOf(i)] = cue;
    }
    r.seek(complexOff);
    for (int i = 0; i < numComplex && !r.bad; i++) {
        uint8_t flags = r.u8();
        Cue cue;
        if ((flags >> 2) & 1) {
            uint32_t soundOff = r.u32();
            r.u32();
            readSound(r, soundOff, cue);
        } else {
            uint32_t varOff = r.u32();
            r.u32();   // transitions
            size_t back = r.pos;
            r.seek(varOff);
            int n = r.u16();
            int vflags = r.u16();
            r.u8(); r.u16(); r.u8();
            int type = (vflags >> 3) & 7;
            for (int j = 0; j < n && !r.bad; j++) {
                if (type == 0 || type == 4) {
                    CueTrack t;
                    t.track = r.u16();
                    t.bank = r.u8();
                    if (type == 0) { int lo = r.u8(), hi = r.u8(); t.weight = std::max(1, hi - lo); }
                    cue.tracks.push_back(t);
                } else if (type == 1 || type == 3) {
                    uint32_t soundOff = r.u32();
                    if (type == 1) r.skip(2); else r.skip(12);
                    Cue sub;
                    readSound(r, soundOff, sub);
                    for (auto& t : sub.tracks) cue.tracks.push_back(t);
                    cue.loop |= sub.loop;
                    cue.volume = sub.volume;
                    cue.category = sub.category;
                } else {
                    break;
                }
            }
            r.seek(back);
        }
        r.skip(1 + 2 + 2 + 1);   // instance limits + fades
        cues[nameOf(numSimple + i)] = cue;
    }
    if (r.bad && cues.empty()) { err = "sound bank cut short"; return false; }
    return true;
}

}  // namespace audio
