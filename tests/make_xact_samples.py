#!/usr/bin/env python3
"""Writes a tiny synthetic XACT set (test.xwb + test.xsb) for audio_test:
  wave 0: 22050 Hz mono PCM16 tone
  wave 1: 44100 Hz stereo MS-ADPCM tone (blockAlign 140/channel)
  wave 2: 11025 Hz mono PCM8 tone
cues: "beep" (simple sound -> wave 0), "song" (complex sound, play-wave event,
looping forever -> wave 1), "rand" (variation table -> waves 0 or 2).
Also writes the expected PCM for each wave (*.raw). Everything is generated.

Usage: make_xact_samples.py OUTDIR
"""
import math
import os
import struct
import sys

ADAPT = [230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230]
COEF1 = [256, 512, 0, 192, 240, 460, 392]
COEF2 = [0, -256, 0, 64, 0, -208, -232]


def tone(rate, secs, freq, ch):
    n = int(rate * secs)
    out = []
    for i in range(n):
        for c in range(ch):
            out.append(int(12000 * math.sin(2 * math.pi * (freq + 110 * c) * i / rate)))
    return out


def adpcm_encode(samples, ch, block_align):
    """Simple MS-ADPCM encoder (predictor 1, greedy nibbles). Returns bytes and
    the PCM a correct decoder must produce."""
    fpb = (block_align - 7 * ch) * 2 // ch + 2
    frames = len(samples) // ch
    out = bytearray()
    decoded = []
    for b0 in range(0, frames - fpb + 1, fpb):
        blk = [samples[(b0 + f) * ch:(b0 + f + 1) * ch] for f in range(fpb)]
        pred = 1
        s2 = [blk[0][c] for c in range(ch)]
        s1 = [blk[1][c] for c in range(ch)]
        delta = [max(16, abs(s1[c] - s2[c]) // 2) for c in range(ch)]
        hdr = bytes([pred] * ch)
        hdr += b"".join(struct.pack("<h", d) for d in delta)
        hdr += b"".join(struct.pack("<h", v) for v in s1)
        hdr += b"".join(struct.pack("<h", v) for v in s2)
        dec = [list(s2), list(s1)]
        nibbles = []
        for f in range(2, fpb):
            row = []
            for c in range(ch):
                p = (s1[c] * COEF1[pred] + s2[c] * COEF2[pred])
                p = int(p / 256)   # C-style truncation
                e = blk[f][c] - p
                q = int(round(e / delta[c]))
                q = max(-8, min(7, q))
                nib = q & 15
                v = max(-32768, min(32767, p + q * delta[c]))
                s2[c], s1[c] = s1[c], v
                delta[c] = max(16, ADAPT[nib] * delta[c] // 256)
                nibbles.append(nib)
                row.append(v)
            dec.append(row)
        body = bytearray()
        for i in range(0, len(nibbles), 2):
            body.append((nibbles[i] << 4) | nibbles[i + 1])
        blockb = hdr + bytes(body)
        assert len(blockb) == block_align, (len(blockb), block_align)
        out += blockb
        for row in dec:
            decoded += row
    return bytes(out), decoded


def mini(codec, ch, rate, align, bits16):
    return codec | (ch << 2) | (rate << 5) | (align << 23) | ((1 if bits16 else 0) << 31)


def make_xwb(path, waves):
    """waves: list of (format, bytes)"""
    entries, data = [], bytearray()
    for fmt, b in waves:
        entries.append((fmt, len(data), len(b)))
        data += b
        while len(data) % 4:
            data.append(0)
    hdr_size = 4 + 4 + 4 + 5 * 8
    bank = struct.pack("<II", 0, len(waves)) + b"Test Bank".ljust(64, b"\0") + struct.pack("<III", 24, 64, 4) + b"\0" * 8
    meta = b"".join(struct.pack("<IIIIII", 0, fmt, off, ln, 0, 0) for fmt, off, ln in entries)
    seg0 = hdr_size
    seg1 = seg0 + len(bank)
    seg4 = seg1 + len(meta)
    head = b"WBND" + struct.pack("<II", 46, 44)
    segs = [(seg0, len(bank)), (seg1, len(meta)), (0, 0), (0, 0), (seg4, len(data))]
    head += b"".join(struct.pack("<II", o, l) for o, l in segs)
    open(path, "wb").write(head + bank + meta + bytes(data))


def make_xsb(path):
    # layout: header, bank names, cue names, simple cues, complex cues, sounds, clip, variation
    names = b"beep\0song\0rand\0"
    hdr_len = 4 + 2 + 2 + 2 + 8 + 1 + 2 + 2 + 2 + 2 + 1 + 2 + 2 + 2 + 10 * 4 + 64
    bank_names_off = hdr_len
    names_off = bank_names_off + 64
    simple_off = names_off + len(names)
    complex_off = simple_off + 5
    # complex: 'song' (flag bit 2: one sound) = 1+4+4+6 ; 'rand' (variation) = 1+4+4+6
    sound1_off = complex_off + 15 + 15
    sound1 = bytes([0]) + struct.pack("<HBhBH", 2, 0xB4, 0, 0, 0) + struct.pack("<HB", 0, 0)   # simple -> wave 0
    sound2_off = sound1_off + len(sound1)
    clip_off = sound2_off + 19
    sound2 = bytes([1]) + struct.pack("<HBhBH", 3, 0xB4, 0, 0, 0) + bytes([1]) + \
        struct.pack("<BIHH", 0xB4, clip_off, 0, 0)
    assert len(sound2) == 19
    event = struct.pack("<IH", 1 | (0 << 5), 0) + bytes([0, 0]) + struct.pack("<HBBHH", 1, 0, 255, 0, 0)
    clip = bytes([1]) + event
    var_off = clip_off + len(clip)
    var = struct.pack("<HHBHB", 2, 0 << 3, 0, 0, 0) + struct.pack("<HBBB", 0, 0, 0, 3) + struct.pack("<HBBB", 2, 0, 0, 1)
    simple = bytes([0]) + struct.pack("<I", sound1_off)
    complex_ = bytes([4]) + struct.pack("<II", sound2_off, 0) + b"\0" * 6
    complex_ += bytes([0]) + struct.pack("<II", var_off, 0) + b"\0" * 6
    head = b"SDBK" + struct.pack("<HHH", 43, 46, 0) + b"\0" * 8 + bytes([1])
    head += struct.pack("<HHHHBHHH", 1, 2, 0, 3, 1, 2, len(names), 0)
    head += struct.pack("<10I", simple_off, complex_off, names_off, 0, 0, 0, bank_names_off, 0, 0, 0)
    head += b"Test Sounds".ljust(64, b"\0")
    assert len(head) == hdr_len, (len(head), hdr_len)
    body = b"Test Bank".ljust(64, b"\0") + names + simple + complex_ + sound1 + sound2 + clip + var
    open(path, "wb").write(head + body)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    w0 = tone(22050, 0.5, 440, 1)
    w1src = tone(44100, 1.0, 330, 2)
    w1, w1dec = adpcm_encode(w1src, 2, 140 * 2)
    w2 = [max(-128, min(127, s // 256)) for s in tone(11025, 0.3, 660, 1)]
    waves = [
        (mini(0, 1, 22050, 2, True), struct.pack(f"<{len(w0)}h", *w0)),
        (mini(2, 2, 44100, 140 - 22, False), w1),
        (mini(0, 1, 11025, 1, False), bytes((s + 128) & 255 for s in w2)),
    ]
    make_xwb(os.path.join(out, "test.xwb"), waves)
    make_xsb(os.path.join(out, "test.xsb"))
    open(os.path.join(out, "w0.raw"), "wb").write(struct.pack(f"<{len(w0)}h", *w0))
    open(os.path.join(out, "w1.raw"), "wb").write(struct.pack(f"<{len(w1dec)}h", *w1dec))
    open(os.path.join(out, "w1src.raw"), "wb").write(struct.pack(f"<{len(w1dec)}h", *w1src[:len(w1dec)]))
    open(os.path.join(out, "w2.raw"), "wb").write(struct.pack(f"<{len(w2)}h", *[s << 8 for s in w2]))


if __name__ == "__main__":
    main()
