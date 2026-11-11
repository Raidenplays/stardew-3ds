#!/usr/bin/env python3
"""Makes the HOME Menu banner (meta/banner.png, 256x128) and its jingle
(meta/banner.wav) for the .cia. Original art: the app icon's sprout on a sky,
with the title. Nothing from the game.  Usage: make_banner.py ICON OUTDIR"""
import math
import os
import struct
import sys
import wave

from PIL import Image, ImageDraw, ImageFont


def main():
    icon, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    W, H = 256, 128
    img = Image.new("RGBA", (W, H))
    d = ImageDraw.Draw(img)
    for y in range(H):   # sky to grass
        k = y / H
        d.line([(0, y), (W, y)], fill=(int(108 + 40 * k), int(180 + 30 * k), int(236 - 20 * k), 255))
    d.rectangle([0, 92, W, H], fill=(88, 156, 60, 255))
    d.rectangle([0, 92, W, 95], fill=(120, 190, 80, 255))
    d.rectangle([0, 104, W, H], fill=(134, 88, 52, 255))
    ic = Image.open(icon).convert("RGBA").resize((96, 96), Image.NEAREST)   # crisp 2x pixels
    img.alpha_composite(ic, (12, 14))
    # pixel text: the classic bitmap font, scaled up by whole pixels
    def pixel_text(text, scale, color, shadow):
        f = ImageFont.load_default()
        if hasattr(f, "size"):          # newer Pillow: ask for the bitmap font explicitly
            try:
                f = ImageFont.load_default(size=None)
            except TypeError:
                pass
        box = f.getbbox(text)
        t = Image.new("RGBA", (int(box[2] * 1.3) + 8, box[3] + 4))
        td = ImageDraw.Draw(t)
        td.fontmode = "1"   # no smoothing: hard pixels
        td.text((1, 1), text, font=f, fill=shadow)
        td.text((0, 0), text, font=f, fill=color)
        return t.resize((t.width * scale, t.height * scale), Image.NEAREST)
    title = pixel_text("stardew3ds", 2, (255, 240, 200, 255), (60, 34, 14, 255))
    img.alpha_composite(title, (118, 34))
    by = pixel_text("Ported by ItsRaiden", 1, (255, 255, 255, 255), (40, 70, 30, 255))
    img.alpha_composite(by, (120, 64))
    img.save(os.path.join(out, "banner.png"))

    # a short two-note chime
    rate, n = 22050, int(22050 * 1.2)
    frames = bytearray()
    for i in range(n):
        t = i / rate
        v = 0.0
        for start, f in ((0.0, 659.25), (0.18, 987.77)):
            if t >= start:
                tt = t - start
                v += math.sin(2 * math.pi * f * tt) * math.exp(-tt * 4) * 0.35
        frames += struct.pack("<h", int(max(-1, min(1, v)) * 32000))
    w = wave.open(os.path.join(out, "banner.wav"), "wb")
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(rate)
    w.writeframes(bytes(frames))
    w.close()


if __name__ == "__main__":
    main()
