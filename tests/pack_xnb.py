#!/usr/bin/env python3
"""Dev/test helper: repacks unpacked files (PNG textures, SmallFont .json+.png)
back into LZX-compressed .xnb, so the on-console converter can be tested
against real-shaped files without the packed Steam originals.

Usage: pack_xnb.py UNPACKED_CONTENT OUT_CONTENT path [path...]
  path = content path without extension, e.g. LooseSprites/Cursors
         or Fonts/SmallFont (a SpriteFont from SmallFont.json + .png)
"""
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_xnb_samples import TEX_R, body, v7, wrap  # noqa: E402
from PIL import Image  # noqa: E402

FONT_R = "Microsoft.Xna.Framework.Content.SpriteFontReader"
LIST_RECT = "Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Rectangle, MonoGame.Framework]]"
LIST_CHAR = "Microsoft.Xna.Framework.Content.ListReader`1[[System.Char, mscorlib]]"
LIST_V3 = "Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Vector3, MonoGame.Framework]]"


def tex_payload(img):
    img = img.convert("RGBA")
    w, h = img.size
    px = bytearray(img.tobytes())
    for i in range(0, len(px), 4):           # premultiply like the XNA pipeline
        a = px[i + 3]
        if a != 255:
            for c in range(3):
                px[i + c] = px[i + c] * a // 255
    return struct.pack("<iIII", 0, w, h, 1) + struct.pack("<I", len(px)) + bytes(px)


def pack_texture(src_png):
    return body([TEX_R], v7(1) + tex_payload(Image.open(src_png)))


def pack_font(json_path, png_path):
    d = json.load(open(json_path, encoding="utf-8-sig"))
    glyphs = list(d["Glyphs"].values())
    readers = [FONT_R, TEX_R, LIST_RECT, LIST_CHAR, LIST_V3]
    out = v7(1) + v7(2) + tex_payload(Image.open(png_path))
    out += v7(3) + struct.pack("<I", len(glyphs))
    for g in glyphs:
        b = g["BoundsInTexture"]
        out += struct.pack("<iiii", b["X"], b["Y"], b["Width"], b["Height"])
    out += v7(3) + struct.pack("<I", len(glyphs))
    for g in glyphs:
        c = g["Cropping"]
        out += struct.pack("<iiii", c["X"], c["Y"], c["Width"], c["Height"])
    out += v7(4) + struct.pack("<I", len(glyphs))
    for g in glyphs:
        out += g["Character"].encode("utf-8")
    out += struct.pack("<if", int(d.get("LineSpacing", 33)), float(d.get("Spacing", 0)))
    out += v7(5) + struct.pack("<I", len(glyphs))
    for g in glyphs:
        out += struct.pack("<fff", g.get("LeftSideBearing", 0), g.get("Width", 0), g.get("RightSideBearing", 0))
    dc = d.get("DefaultCharacter")
    out += (b"\x01" + dc.encode("utf-8")) if dc else b"\x00"
    return body(readers, out)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    for p in sys.argv[3:]:
        os.makedirs(os.path.dirname(os.path.join(dst, p)), exist_ok=True)
        if p.startswith("Fonts/"):
            data = pack_font(os.path.join(src, p + ".json"), os.path.join(src, p + ".png"))
        else:
            data = pack_texture(os.path.join(src, p + ".png"))
        open(os.path.join(dst, p + ".xnb"), "wb").write(wrap(data, "lzx"))
        print("packed", p)


if __name__ == "__main__":
    main()
