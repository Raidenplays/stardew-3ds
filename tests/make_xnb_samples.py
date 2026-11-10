#!/usr/bin/env python3
"""Builds synthetic .xnb files (uncompressed, LZ4 and LZX) for xnb_test.
All content is generated here; nothing comes from the game.

Usage: make_xnb_samples.py OUTDIR
"""
import os
import random
import struct
import sys

STR_R = "Microsoft.Xna.Framework.Content.StringReader"
TEX_R = "Microsoft.Xna.Framework.Content.Texture2DReader"
DICT_SS = ("Microsoft.Xna.Framework.Content.DictionaryReader`2[[System.String, mscorlib, Version=4.0.0.0, "
           "Culture=neutral, PublicKeyToken=b77a5c561934e089],[System.String, mscorlib, Version=4.0.0.0, "
           "Culture=neutral, PublicKeyToken=b77a5c561934e089]]")
DICT_IS = DICT_SS.replace("[[System.String", "[[System.Int32", 1)
INT_R = "Microsoft.Xna.Framework.Content.Int32Reader"


def v7(n):
    out = b""
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out += bytes([b | 0x80])
        else:
            return out + bytes([b])


def s7(s):
    b = s.encode()
    return v7(len(b)) + b


def body(readers, payload):
    out = v7(len(readers))
    for r in readers:
        out += s7(r) + struct.pack("<i", 0)
    return out + v7(0) + payload


# ---------------------------------------------------------------- LZ4 (literals + simple matches)
def lz4_compress(data):
    out = bytearray()
    i, lit_start, n = 0, 0, len(data)
    table = {}

    def emit(lit, off, mlen):
        ll = len(lit)
        ml = mlen - 4 if mlen else 0
        tok = (min(ll, 15) << 4) | (min(ml, 15) if mlen else 0)
        out.append(tok)
        if ll >= 15:
            r = ll - 15
            while r >= 255:
                out.append(255); r -= 255
            out.append(r)
        out.extend(lit)
        if mlen:
            out.extend(struct.pack("<H", off))
            if ml >= 15:
                r = ml - 15
                while r >= 255:
                    out.append(255); r -= 255
                out.append(r)

    while i + 12 < n:
        key = data[i:i + 4]
        j = table.get(key)
        table[key] = i
        if j is not None and i - j < 65535 and data[j:j + 4] == key:
            m = 4
            while i + m < n - 5 and data[j + m] == data[i + m]:
                m += 1
            emit(data[lit_start:i], i - j, m)
            i += m
            lit_start = i
        else:
            i += 1
    emit(data[lit_start:], 0, 0)
    return bytes(out)


# ---------------------------------------------------------------- LZX (verbatim blocks, fixed code lengths)
EXTRA = []
j = 0
for i in range(0, 51, 2):
    EXTRA += [j, j]
    if i != 0 and j < 17:
        j += 1
EXTRA = EXTRA[:51]
BASE = [0]
for e in EXTRA:
    BASE.append(BASE[-1] + (1 << e))


class BitW:
    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.n = 0

    def put(self, v, bits):
        for k in range(bits - 1, -1, -1):
            self.acc = (self.acc << 1) | ((v >> k) & 1)
            self.n += 1
            if self.n == 16:
                self.out += struct.pack("<H", self.acc)
                self.acc = 0
                self.n = 0

    def flush(self):
        if self.n:
            self.put(0, 16 - self.n)
        return bytes(self.out)


def lzx_compress(data):
    """Returns the XNA-framed LZX stream for data."""
    FRAME = 0x8000
    r = [1, 1, 1]
    chunks = []
    total = len(data)
    first = True
    pos = 0
    rng_hash = {}
    while pos < total:
        frame_end = min(pos + FRAME, total)
        w = BitW()
        if first:
            w.put(0, 1)                    # no Intel E8 translation
            w.put(1, 3)                    # verbatim block covering everything
            w.put(total >> 8, 16)
            w.put(total & 0xFF, 8)
            for (lo, hi, ln) in ((0, 256, 9), (256, 512, 9), (0, 249, 8)):
                for _ in range(20):
                    w.put(5, 4)            # pretree: 20 symbols x 5 bits
                z = (0 - ln + 17) % 17
                for _ in range(lo, hi):
                    w.put(z, 5)
            first = False
        i = pos
        while i < frame_end:
            best_len, best_off = 0, 0
            if i + 3 <= frame_end:
                key = data[i:i + 3]
                for cand in rng_hash.get(key, [])[-8:]:
                    off = i - cand
                    if off <= 0 or off >= 32000:
                        continue
                    m = 0
                    while i + m < frame_end and m < 257 and data[cand + m] == data[i + m]:
                        m += 1
                    if m > best_len or (m == best_len and off == r[0]):
                        best_len, best_off = m, off
                # also try the repeat offset
                off = r[0]
                if i - off >= 0:
                    m = 0
                    while i + m < frame_end and m < 257 and data[i - off + m] == data[i + m]:
                        m += 1
                    if m >= max(best_len, 2):
                        best_len, best_off = m, off
            if best_len >= 3:
                L = best_len
                off = best_off
                if off == r[0]:
                    slot = 0
                else:
                    f = off + 2
                    slot = 3
                    while BASE[slot + 1] <= f:
                        slot += 1
                    r = [off, r[0], r[1]]
                hdr = min(L - 2, 7)
                w.put(256 + slot * 8 + hdr, 9)
                if hdr == 7:
                    w.put(L - 2 - 7, 8)
                if slot >= 3:
                    w.put(off + 2 - BASE[slot], EXTRA[slot])
                for k in range(L):
                    if i + k + 3 <= total:
                        rng_hash.setdefault(data[i + k:i + k + 3], []).append(i + k)
                i += L
            else:
                w.put(data[i], 9)
                if i + 3 <= total:
                    rng_hash.setdefault(data[i:i + 3], []).append(i)
                i += 1
        comp = w.flush()
        n = frame_end - pos
        if n == FRAME:
            chunks.append(struct.pack(">H", len(comp)) + comp)
        else:
            chunks.append(b"\xFF" + struct.pack(">HH", n, len(comp)) + comp)
        pos = frame_end
    return b"".join(chunks)


def wrap(payload, mode):
    if mode == "raw":
        return b"XNBw\x05\x00" + struct.pack("<I", 10 + len(payload)) + payload
    comp = lz4_compress(payload) if mode == "lz4" else lzx_compress(payload)
    flag = 0x40 if mode == "lz4" else 0x80
    return b"XNBw\x05" + bytes([flag]) + struct.pack("<II", 14 + len(comp), len(payload)) + comp


def texture(w, h, seed):
    rnd = random.Random(seed)
    px = bytearray()
    for y in range(h):
        for x in range(w):
            if (x // 8 + y // 8) % 3 == 0:
                a = 0
            elif (x * y) % 17 == 0:
                a = rnd.choice([64, 128, 200])
            else:
                a = 255
            r, g, b = (x * 7) & 255, (y * 5) & 255, ((x ^ y) * 3) & 255
            # premultiply, like the XNA content pipeline
            px += bytes([r * a // 255, g * a // 255, b * a // 255, a])
    return bytes(px)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    w, h = 96, 400            # 153,600 bytes: several LZX frames
    px = texture(w, h, 3)
    tex = body([TEX_R], v7(1) + struct.pack("<iIII", 0, w, h, 1) + struct.pack("<I", len(px)) + px)
    for mode in ("raw", "lz4", "lzx"):
        open(os.path.join(out, f"tex_{mode}.xnb"), "wb").write(wrap(tex, mode))
    open(os.path.join(out, "tex_premul.rgba"), "wb").write(px)

    entries = [(f"key{i}", f"value {i}/" + "x" * (i % 40)) for i in range(300)]
    payload = v7(1) + struct.pack("<I", len(entries))
    for k, v in entries:
        payload += v7(2) + s7(k) + v7(2) + s7(v)
    d = body([DICT_SS, STR_R], payload)
    open(os.path.join(out, "dict_lzx.xnb"), "wb").write(wrap(d, "lzx"))

    payload = v7(1) + struct.pack("<I", 3)
    for k, v in ((5, "five"), (17, "seventeen"), (-2, "neg")):
        payload += struct.pack("<i", k) + v7(3) + s7(v)
    d = body([DICT_IS, INT_R, STR_R], payload)
    open(os.path.join(out, "dict_int.xnb"), "wb").write(wrap(d, "raw"))


if __name__ == "__main__":
    main()
