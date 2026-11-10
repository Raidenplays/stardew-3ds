#include "setup/lzx.h"
#include <cstring>

namespace lzx {
namespace {

constexpr int WINDOW_BITS = 16;                 // XNA always uses a 64 KiB window
constexpr uint32_t WINDOW_SIZE = 1u << WINDOW_BITS;
constexpr int POSITION_SLOTS = 32;              // for a 64 KiB window
constexpr int MAIN_ELEMS = 256 + POSITION_SLOTS * 8;
constexpr int LEN_ELEMS = 249;
constexpr int ALIGNED_ELEMS = 8;
constexpr int PRETREE_ELEMS = 20;
constexpr int MAX_BITS = 16;

uint8_t EXTRA_BITS[51];
uint32_t POSITION_BASE[51];
bool tablesReady = false;

void initTables() {
    if (tablesReady) return;
    for (int i = 0, j = 0; i < 51; i += 2) {
        EXTRA_BITS[i] = (uint8_t)j;
        if (i + 1 < 51) EXTRA_BITS[i + 1] = (uint8_t)j;
        if (i != 0 && j < 17) j++;
    }
    uint32_t b = 0;
    for (int i = 0; i < 51; i++) { POSITION_BASE[i] = b; b += 1u << EXTRA_BITS[i]; }
    tablesReady = true;
}

// Canonical Huffman decoder with a 10-bit fast table and a slow path.
struct Huff {
    static constexpr int FAST = 10;
    int n = 0;
    uint16_t count[MAX_BITS + 1];
    uint16_t symbols[MAIN_ELEMS];
    int16_t fast[1 << FAST];      // sym | (len << 12), or -1
    bool empty = true;

    bool build(const uint8_t* lens, int num) {
        n = num;
        memset(count, 0, sizeof count);
        for (int i = 0; i < num; i++) count[lens[i]]++;
        count[0] = 0;
        empty = true;
        for (int l = 1; l <= MAX_BITS; l++) if (count[l]) empty = false;
        uint16_t offs[MAX_BITS + 2];
        offs[1] = 0;
        for (int l = 1; l <= MAX_BITS; l++) offs[l + 1] = offs[l] + count[l];
        for (int i = 0; i < num; i++) if (lens[i]) symbols[offs[lens[i]]++] = (uint16_t)i;
        // check over-subscription
        int left = 1;
        for (int l = 1; l <= MAX_BITS; l++) { left <<= 1; left -= count[l]; if (left < 0) return false; }
        // fast table
        for (int i = 0; i < (1 << FAST); i++) fast[i] = -1;
        int code = 0, idx = 0;
        for (int l = 1; l <= FAST; l++) {
            for (int k = 0; k < count[l]; k++, idx++, code++) {
                int shift = FAST - l;
                for (int f = 0; f < (1 << shift); f++)
                    fast[(code << shift) | f] = (int16_t)(symbols[idx] | (l << 12));
            }
            code <<= 1;
        }
        return true;
    }
};

struct Bits {
    const uint8_t* p; const uint8_t* end;
    uint32_t buf = 0; int left = 0;
    void init(const uint8_t* s, const uint8_t* e) { p = s; end = e; buf = 0; left = 0; }
    void ensure(int n) {
        while (left < n) {
            uint32_t w = 0;
            if (p + 1 < end) { w = p[0] | (p[1] << 8); }
            else if (p < end) { w = p[0]; }
            p += 2;
            buf |= w << (16 - left);   // buf is MSB-aligned in 32 bits
            left += 16;
        }
    }
    uint32_t peek(int n) { ensure(n); return buf >> (32 - n); }
    void drop(int n) { buf <<= n; left -= n; }
    uint32_t read(int n) {
        if (n == 0) return 0;
        if (n > 16) { uint32_t hi = read(16); return (hi << (n - 16)) | read(n - 16); }
        uint32_t v = peek(n); drop(n); return v;
    }
    bool overrun() const { return p > end + 4; }
};

int decodeSym(Bits& b, const Huff& h) {
    b.ensure(MAX_BITS);
    int f = h.fast[b.buf >> (32 - Huff::FAST)];
    if (f >= 0) { b.drop(f >> 12); return f & 0xFFF; }
    // slow canonical walk
    int code = 0, first = 0, index = 0;
    for (int l = 1; l <= MAX_BITS; l++) {
        code |= (b.buf >> (31 - (l - 1))) & 1;
        int c = h.count[l];
        if (code - c < first) { b.drop(l); return h.symbols[index + (code - first)]; }
        index += c; first += c; first <<= 1; code <<= 1;
    }
    return -1;
}

struct State {
    uint8_t window[WINDOW_SIZE];
    uint32_t pos = 0;
    uint32_t R0 = 1, R1 = 1, R2 = 1;
    uint8_t mainLen[MAIN_ELEMS] = {};
    uint8_t lenLen[LEN_ELEMS] = {};
    uint8_t alignedLen[ALIGNED_ELEMS] = {};
    Huff mainT, lenT, alignedT;
    bool headerRead = false;
    bool intel = false;
    int32_t intelSize = 0;
    int blockType = 0;
    uint32_t blockRemaining = 0;
    uint32_t blockLen = 0;
    uint32_t framesDone = 0;
};

bool readLengths(Bits& b, uint8_t* lens, int first, int last) {
    uint8_t pre[PRETREE_ELEMS];
    for (int i = 0; i < PRETREE_ELEMS; i++) pre[i] = (uint8_t)b.read(4);
    Huff pt;
    if (!pt.build(pre, PRETREE_ELEMS)) return false;
    for (int x = first; x < last;) {
        int z = decodeSym(b, pt);
        if (z < 0) return false;
        if (z == 17) {
            int y = (int)b.read(4) + 4;
            while (y-- && x < last) lens[x++] = 0;
        } else if (z == 18) {
            int y = (int)b.read(5) + 20;
            while (y-- && x < last) lens[x++] = 0;
        } else if (z == 19) {
            int y = (int)b.read(1) + 4;
            int s = decodeSym(b, pt);
            if (s < 0) return false;
            int v = (lens[x] - s + 17) % 17;
            while (y-- && x < last) lens[x++] = (uint8_t)v;
        } else {
            lens[x] = (uint8_t)((lens[x] - z + 17) % 17);
            x++;
        }
    }
    return true;
}

// Decodes one XNA chunk: `in` holds blockSize compressed bytes that expand to
// frameSize output bytes, appended to out.
bool decodeFrame(State& s, const uint8_t* in, size_t inSize, uint32_t frameSize, std::vector<uint8_t>& out) {
    Bits b;
    b.init(in, in + inSize);
    if (!s.headerRead) {
        if (b.read(1)) { uint32_t hi = b.read(16), lo = b.read(16); s.intel = true; s.intelSize = (int32_t)((hi << 16) | lo); }
        s.headerRead = true;
    }
    uint32_t frameStart = s.pos;
    int32_t togo = (int32_t)frameSize;
    while (togo > 0) {
        if (s.blockRemaining == 0) {
            s.blockType = (int)b.read(3);
            uint32_t hi = b.read(16), lo = b.read(8);
            s.blockRemaining = s.blockLen = (hi << 8) | lo;
            if (s.blockType == 2) {
                for (int i = 0; i < ALIGNED_ELEMS; i++) s.alignedLen[i] = (uint8_t)b.read(3);
                if (!s.alignedT.build(s.alignedLen, ALIGNED_ELEMS)) return false;
            }
            if (s.blockType == 1 || s.blockType == 2) {
                if (!readLengths(b, s.mainLen, 0, 256)) return false;
                if (!readLengths(b, s.mainLen, 256, MAIN_ELEMS)) return false;
                if (!s.mainT.build(s.mainLen, MAIN_ELEMS)) return false;
                if (s.mainLen[0xE8]) s.intel = true;
                if (!readLengths(b, s.lenLen, 0, LEN_ELEMS)) return false;
                if (!s.lenT.build(s.lenLen, LEN_ELEMS)) return false;
            } else if (s.blockType == 3) {
                s.intel = true;
                // realign to 16 bits: drop what's buffered in the current word
                b.ensure(16);
                if (b.left > 16) b.p -= 2;
                b.left = 0; b.buf = 0;
                auto rd32 = [&](uint32_t& r) { r = b.p[0] | (b.p[1] << 8) | (b.p[2] << 16) | ((uint32_t)b.p[3] << 24); b.p += 4; };
                rd32(s.R0); rd32(s.R1); rd32(s.R2);
            } else {
                return false;
            }
        }
        while (s.blockRemaining > 0 && togo > 0) {
            int32_t run = (int32_t)s.blockRemaining;
            if (run > togo) run = togo;
            togo -= run;
            s.blockRemaining -= (uint32_t)run;
            if (s.blockType == 3) {
                while (run-- > 0) { s.window[s.pos] = *b.p++; s.pos = (s.pos + 1) & (WINDOW_SIZE - 1); }
                if (s.blockRemaining == 0 && (s.blockLen & 1)) b.p++;   // pad byte
                continue;
            }
            while (run > 0) {
                int m = decodeSym(b, s.mainT);
                if (m < 0) return false;
                if (m < 256) {
                    s.window[s.pos] = (uint8_t)m;
                    s.pos = (s.pos + 1) & (WINDOW_SIZE - 1);
                    run--;
                    continue;
                }
                m -= 256;
                int len = m & 7;
                if (len == 7) { int f = decodeSym(b, s.lenT); if (f < 0) return false; len += f; }
                len += 2;
                int slot = m >> 3;
                uint32_t off;
                if (slot > 2) {
                    int extra = EXTRA_BITS[slot];
                    off = POSITION_BASE[slot] - 2;
                    if (s.blockType == 2) {
                        if (extra > 3) {
                            off += b.read(extra - 3) << 3;
                            int a = decodeSym(b, s.alignedT); if (a < 0) return false;
                            off += (uint32_t)a;
                        } else if (extra == 3) {
                            int a = decodeSym(b, s.alignedT); if (a < 0) return false;
                            off += (uint32_t)a;
                        } else if (extra > 0) {
                            off += b.read(extra);
                        } else {
                            off = 1;
                        }
                    } else {
                        off += b.read(extra);
                    }
                    s.R2 = s.R1; s.R1 = s.R0; s.R0 = off;
                } else if (slot == 0) {
                    off = s.R0;
                } else if (slot == 1) {
                    off = s.R1; s.R1 = s.R0; s.R0 = off;
                } else {
                    off = s.R2; s.R2 = s.R0; s.R0 = off;
                }
                run -= len;
                uint32_t src = (s.pos - off) & (WINDOW_SIZE - 1);
                while (len-- > 0) {
                    s.window[s.pos] = s.window[src];
                    s.pos = (s.pos + 1) & (WINDOW_SIZE - 1);
                    src = (src + 1) & (WINDOW_SIZE - 1);
                }
            }
            // a match may run past the frame end; carry the overshoot
            if (run < 0) {
                if ((uint32_t)(-run) > s.blockRemaining) return false;
                s.blockRemaining -= (uint32_t)(-run);
                togo += run;
            }
        }
        if (b.overrun()) return false;
    }
    // copy frame out of the window
    size_t base = out.size();
    out.resize(base + frameSize);
    for (uint32_t i = 0; i < frameSize; i++) out[base + i] = s.window[(frameStart + i) & (WINDOW_SIZE - 1)];
    // undo Intel E8 call translation
    if (s.intel && s.intelSize > 0 && s.framesDone < 32768 && frameSize > 10) {
        uint8_t* d = &out[base];
        int32_t curpos = (int32_t)(s.framesDone * 32768u);
        int32_t endi = (int32_t)frameSize - 10;
        for (int32_t i = 0; i < endi;) {
            if (d[i++] != 0xE8) { curpos++; continue; }
            int32_t absOff = (int32_t)(d[i] | (d[i + 1] << 8) | (d[i + 2] << 16) | ((uint32_t)d[i + 3] << 24));
            if (absOff >= -curpos && absOff < s.intelSize) {
                int32_t rel = absOff > 0 ? absOff - curpos : absOff + s.intelSize;
                d[i] = (uint8_t)rel; d[i + 1] = (uint8_t)(rel >> 8); d[i + 2] = (uint8_t)(rel >> 16); d[i + 3] = (uint8_t)(rel >> 24);
            }
            i += 4; curpos += 5;
        }
    }
    s.framesDone++;
    return true;
}

}  // namespace

bool decompressXnb(const uint8_t* in, size_t inSize, std::vector<uint8_t>& out, size_t outSize) {
    initTables();
    State* s = new State();
    out.clear();
    out.reserve(outSize);
    size_t pos = 0;
    bool ok = true;
    while (pos + 2 <= inSize && out.size() < outSize) {
        uint32_t hi = in[pos], lo = in[pos + 1];
        uint32_t block = (hi << 8) | lo, frame = 0x8000;
        if (hi == 0xFF) {
            if (pos + 5 > inSize) { ok = false; break; }
            frame = (lo << 8) | in[pos + 2];
            block = ((uint32_t)in[pos + 3] << 8) | in[pos + 4];
            pos += 5;
        } else {
            pos += 2;
        }
        if (block == 0 || frame == 0) break;
        if (pos + block > inSize) { ok = false; break; }
        if (out.size() + frame > outSize) frame = (uint32_t)(outSize - out.size());
        if (!decodeFrame(*s, in + pos, block, frame, out)) { ok = false; break; }
        pos += block;
    }
    delete s;
    return ok && out.size() == outSize;
}

}  // namespace lzx
