"""Unpacks a normal (packed) Stardew Valley Content folder into the layout the
converter works from -- the same files StardewXnbHack makes: textures as
.png, maps as .tmx, data and strings as .json, fonts as .json + .png.

Everything comes from the player's own game files. Data models need
StardewValley.GameData.dll (next to Content in the game folder) for their
layout; plain string tables don't.

    python3 xnbunpack.py <Content folder> <output folder>
"""
import json
import os
import re
import sys
import time

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import tbin  # noqa: E402
import xnbfile  # noqa: E402

DLL = "StardewValley.GameData.dll"
LOCALIZED = re.compile(r"\.[a-z]{2}-[A-Z]{2}$")   # Objects.de-DE etc: only English is used
SKIP_DIRS = {"Effects", "XACT"}


def is_packed(content):
    """True for a Content folder of .xnb files (no unpacked maps)."""
    maps = os.path.join(content, "Maps")
    if not os.path.isdir(maps):
        return False
    names = os.listdir(maps)
    return any(n.lower().endswith(".xnb") for n in names) and not any(n.lower().endswith(".tmx") for n in names)


def find_dll(content, extra=()):
    """StardewValley.GameData.dll: inside the Content folder or zip you picked,
    next to it (the game folder), or in one of `extra`."""
    cands = [content, os.path.dirname(content.rstrip("/\\"))] + list(extra)
    for d in cands:
        if not d or not os.path.isdir(d):
            continue
        p = os.path.join(d, DLL)
        if os.path.isfile(p):
            return p
    for d in cands[:1] + list(extra):
        if d and os.path.isdir(d):
            for dirpath, _, files in os.walk(d):
                if DLL in files:
                    return os.path.join(dirpath, DLL)
    return None


def _texture(r, w=None):
    fmt = r.i32()
    w, h = r.u32(), r.u32()
    mips = r.u32()
    size = r.u32()
    if fmt == 0 and size == w * h * 4:
        px = np.frombuffer(r.bytes(size), dtype=np.uint8).reshape(h, w, 4).copy()
    elif fmt in (4, 5, 6):                 # DXT1 / DXT3 / DXT5 (the game's fonts use DXT3)
        px = _dxt(r.bytes(size), w, h, fmt)
    else:
        raise xnbfile.XnbError(f"unsupported texture format {fmt}")
    for _ in range(mips - 1):              # smaller mip levels aren't used
        r.bytes(r.u32())
    # XNA textures are premultiplied; PNGs are straight alpha
    a = px[:, :, 3:4].astype(np.uint32)
    part = (a > 0) & (a < 255)
    if part.any():
        rgb = px[:, :, :3].astype(np.uint32)
        fixed = np.minimum(rgb * 255 // np.maximum(a, 1), 255)   # truncating, like StardewXnbHack
        px[:, :, :3] = np.where(part, fixed, rgb).astype(np.uint8)
    px[(a[:, :, 0] == 0)] = 0
    return Image.fromarray(px, "RGBA")


def _dxt(data, w, h, fmt):
    """Block-compressed texture -> RGBA (still premultiplied, like the raw ones)."""
    bw, bh = (w + 3) // 4, (h + 3) // 4
    bsize = 8 if fmt == 4 else 16
    blk = np.frombuffer(data[:bw * bh * bsize], dtype=np.uint8).reshape(bw * bh, bsize)
    col = blk[:, -8:]
    c0 = col[:, 0].astype(np.uint32) | (col[:, 1].astype(np.uint32) << 8)
    c1 = col[:, 2].astype(np.uint32) | (col[:, 3].astype(np.uint32) << 8)

    def rgb(c):
        r_, g_, b_ = (c >> 11) & 31, (c >> 5) & 63, c & 31
        return np.stack([(r_ << 3) | (r_ >> 2), (g_ << 2) | (g_ >> 4), (b_ << 3) | (b_ >> 2)], 1).astype(np.int32)
    p0, p1 = rgb(c0), rgb(c1)
    four = (c0 > c1) | (fmt != 4)
    p2 = np.where(four[:, None], (2 * p0 + p1) // 3, (p0 + p1) // 2)
    p3 = np.where(four[:, None], (p0 + 2 * p1) // 3, 0)
    pal = np.stack([p0, p1, p2, p3], 1)                         # (n, 4, 3)
    bits = col[:, 4:8].astype(np.uint32)
    idx = np.stack([(bits[:, k // 4] >> (2 * (k % 4))) & 3 for k in range(16)], 1)   # (n, 16)
    out_rgb = np.take_along_axis(pal, idx[:, :, None].repeat(3, 2), 1)
    if fmt == 5:
        ab = blk[:, :8]
        alpha = np.stack([((ab[:, k // 2] >> (4 * (k % 2))) & 15) * 17 for k in range(16)], 1)
    elif fmt == 6:
        a0, a1 = blk[:, 0].astype(np.int32), blk[:, 1].astype(np.int32)
        ab = blk[:, 2:8].astype(np.uint64)
        v = sum(ab[:, k] << np.uint64(8 * k) for k in range(6))
        ai = np.stack([(v >> np.uint64(3 * k)) & np.uint64(7) for k in range(16)], 1).astype(np.int32)
        tab = [a0, a1] + [np.where(a0 > a1, ((7 - k) * a0 + k * a1) // 7, ((5 - k) * a0 + k * a1) // 5) for k in range(1, 5)]
        tab += [np.where(a0 > a1, (2 * a0 + 5 * a1) // 7, 0), np.where(a0 > a1, (a0 + 6 * a1) // 7, 255)]
        alpha = np.take_along_axis(np.stack(tab, 1), ai, 1)
    else:
        alpha = np.where((~four[:, None]) & (idx == 3), 0, 255)
    px = np.concatenate([out_rgb, alpha[:, :, None]], 2).astype(np.uint8)          # (n, 16, 4)
    px = px.reshape(bh, bw, 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(bh * 4, bw * 4, 4)
    return np.ascontiguousarray(px[:h, :w])


def _font(r, dec):
    rd = r.type_id()
    if rd is None or "Texture2DReader" not in rd:
        raise xnbfile.XnbError("font without texture")
    img = _texture(r)
    glyphs = dec.read_object(r)
    crops = dec.read_object(r)
    chars = dec.read_object(r)
    line = r.i32()
    spacing = r.f32()
    kern = dec.read_object(r)            # list of Vector3 "x, y, z"
    has_default = r.u8()
    default = r.char() if has_default else None
    out = {"LineSpacing": line, "Spacing": xnbfile._f(spacing), "DefaultCharacter": default,
           "Characters": chars, "Glyphs": {}}
    for ch, g, c, k in zip(chars, glyphs, crops, kern):
        kx, ky, kz = (k["X"], k["Y"], k["Z"]) if isinstance(k, dict) else (float(v) for v in k.split(","))
        out["Glyphs"][ch] = {"Character": ch, "BoundsInTexture": g, "Cropping": c,
                             "LeftSideBearing": kx, "RightSideBearing": kz, "Width": ky,
                             "WidthIncludingBearings": kx + ky + kz}
    return out, img


def unpack(content, out_dir, dll=None, log=print, only=None):
    """Unpacks every .xnb under `content` into `out_dir` (skipping what's
    already there and up to date). `only` limits it to top-level folders."""
    schema = None
    if dll:
        import netschema
        schema = netschema.Schema(dll)
    dec = xnbfile.Decoder(schema)
    jobs = []
    for dirpath, dirnames, files in os.walk(content):
        rel_dir = os.path.relpath(dirpath, content)
        top = rel_dir.split(os.sep)[0]
        if top in SKIP_DIRS or (only and rel_dir != "." and top not in only):
            dirnames[:] = []
            continue
        for fn in files:
            if fn.lower().endswith(".xnb") and not LOCALIZED.search(fn[:-4]):
                jobs.append(os.path.join(rel_dir, fn) if rel_dir != "." else fn)
    jobs.sort()
    t0 = time.time()
    counts = {"texture": 0, "map": 0, "data": 0, "font": 0, "skipped": 0}
    errors = 0
    no_schema = 0
    for n, rel in enumerate(jobs, 1):
        src = os.path.join(content, rel)
        stem = os.path.join(out_dir, rel[:-4])
        if n % 250 == 0:
            log(f"  unpacking: {n}/{len(jobs)} files...")
        done = [stem + e for e in (".png", ".tmx", ".json")]
        if any(os.path.isfile(p) and os.path.getmtime(p) >= os.path.getmtime(src) for p in done):
            continue
        try:
            r = xnbfile.load(src)
            rd = r.type_id()
            if rd is None:
                continue
            base = xnbfile.split_generic(rd)[0].rsplit(".", 1)[-1]
            os.makedirs(os.path.dirname(stem) or ".", exist_ok=True)
            if base == "Texture2DReader":
                _texture(r).save(stem + ".png", compress_level=1)
                counts["texture"] += 1
            elif base == "TideReader":
                size = r.i32()
                m = tbin.read_tbin_bytes(r.bytes(size))
                m.write_tmx(stem + ".tmx", lambda ts: ts.image)
                counts["map"] += 1
            elif base == "SpriteFontReader":
                data, img = _font(r, dec)
                img.save(stem + ".png", compress_level=1)
                with open(stem + ".json", "w", encoding="utf-8") as f:
                    json.dump(data, f, ensure_ascii=False, indent=2)
                counts["font"] += 1
            elif base in ("DictionaryReader`2", "ListReader`1", "ReflectiveReader`1", "StringReader", "ArrayReader`1"):
                try:
                    v = dec.read_with(r, rd)
                except xnbfile.XnbError as e:
                    if schema is None and "GameData.dll" in str(e):
                        no_schema += 1
                        continue
                    raise
                with open(stem + ".json", "w", encoding="utf-8") as f:
                    json.dump(v, f, ensure_ascii=False, indent=2)
                counts["data"] += 1
            else:
                counts["skipped"] += 1
        except Exception as e:   # noqa: BLE001 -- one odd file shouldn't stop the rest
            errors += 1
            if errors <= 20:
                log(f"  ! {rel}: {e}")
    log(f"  unpacked {counts['texture']} textures, {counts['map']} maps, {counts['data']} data files, "
        f"{counts['font']} fonts in {time.time() - t0:.0f}s" + (f" ({errors} failed)" if errors else ""))
    if no_schema:
        log(f"  ! {no_schema} data files need {DLL} -- copy it from your Stardew Valley folder "
            "(next to Content) into the Content folder you picked, then convert again.")
    return no_schema == 0


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    content, out = sys.argv[1], sys.argv[2]
    dll = find_dll(content)
    if not dll:
        print(f"Note: {DLL} not found, so Data models will be skipped.")
    unpack(content, out, dll)


if __name__ == "__main__":
    main()
