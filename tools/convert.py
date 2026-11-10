#!/usr/bin/env python3
"""
stardew3ds asset converter
==========================

Turns YOUR OWN copy of Stardew Valley's content (unpacked with
StardewXnbHack) into the files the 3DS engine reads from the SD card.

Nothing from the game is included in this project. This script only
reads files you already have.

Usage:
    python3 convert.py "<Stardew Valley>/Content (unpacked)" out/

Then copy   out/3ds/stardew3ds/   to the root of your SD card so you get
    sdmc:/3ds/stardew3ds/tex/...
    sdmc:/3ds/stardew3ds/maps/...
    sdmc:/3ds/stardew3ds/data/...

Requires: Python 3.8+, Pillow, numpy   (pip install pillow numpy)

Output formats (all little-endian):

  .s3t texture
    char[4] "S3T1"; u16 width; u16 height; u16 pageCount; u16 reserved
    per page: u16 y0; u16 h; u16 pw; u16 ph; then pw*ph*4 bytes
    Each page is a horizontal strip of the source image (max 1024 tall),
    padded to power-of-two and pre-swizzled into the 3DS GPU's
    8x8 Morton-tiled RGBA8 layout (byte order A,B,G,R, rows flipped).

  .smap map    see write_smap()
  data/*.dat   tab-separated text, see write_data()
"""
import json
import os
import re
import struct
import sys
import base64
import zlib
import gzip
import xml.etree.ElementTree as ET

try:
    import numpy as np
    from PIL import Image
except ImportError:
    sys.exit("Needs Pillow and numpy:  pip install pillow numpy")

SEASONS = ["spring", "summer", "fall", "winter"]
MAX_PAGE = 1024

# Textures the engine needs besides map tilesheets.
# (content path without extension, required?)
EXTRA_TEXTURES = [
    ("Maps/springobjects", True),
    ("TileSheets/crops", True),
    ("TileSheets/tools", True),
    ("TerrainFeatures/hoeDirt", True),
    ("Characters/Farmer/farmer_base", True),
    ("Characters/Farmer/hairstyles", True),
    ("Characters/Farmer/pants", False),
    ("Characters/Farmer/shirts", False),
    ("Buildings/houses", False),
    ("Buildings/Shipping Bin", False),
    ("LooseSprites/Cursors", False),
    ("LooseSprites/font_bold", False),
]

MAPS = ["Farm"]

# Layers drawn by the engine, in this fixed order.
LAYERS = ["Back", "Buildings", "Paths", "Front", "AlwaysFront"]

# cell flags
F_DIGGABLE = 1
F_WATER = 2
F_BLOCKED = 4
F_NOSPAWN = 8
F_PASSABLE_OVERRIDE = 16


def tex_name(content_path):
    """'Maps/spring_outdoorsTileSheet' -> 'Maps_spring_outdoorsTileSheet'"""
    return content_path.replace("\\", "/").replace("/", "_").replace(" ", "_")


# --------------------------------------------------------------------------
# Textures
# --------------------------------------------------------------------------

def _next_pow2(v):
    p = 8
    while p < v:
        p <<= 1
    return p


def _morton_table(pw, ph):
    """For every (y, x) in a pw*ph texture return the swizzled pixel index.
    y here is GPU row (0 = bottom)."""
    ys, xs = np.mgrid[0:ph, 0:pw]
    tile = (ys >> 3) * (pw >> 3) + (xs >> 3)
    x, y = xs & 7, ys & 7
    morton = ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) |
              ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3))
    return (tile << 6) + morton


def swizzle(rgba):
    """rgba: HxWx4 uint8 (top row first) already padded to pow2.
    Returns bytes in 3DS GPU RGBA8 tiled layout."""
    ph, pw = rgba.shape[:2]
    flipped = rgba[::-1]                      # GPU row 0 is image bottom
    idx = _morton_table(pw, ph).ravel()
    out = np.zeros((pw * ph, 4), dtype=np.uint8)
    # GPU_RGBA8 little-endian word 0xRRGGBBAA -> bytes A B G R
    out[idx] = flipped.reshape(-1, 4)[:, ::-1]
    return out.tobytes()


def convert_texture(src_png, dst_s3t):
    img = Image.open(src_png).convert("RGBA")
    w, h = img.size
    if w > MAX_PAGE:
        print(f"  ! {src_png}: wider than {MAX_PAGE}px, right side cropped")
        img = img.crop((0, 0, MAX_PAGE, h))
        w = MAX_PAGE
    arr = np.asarray(img)
    pages = []
    y = 0
    while y < h:
        ph_real = min(MAX_PAGE, h - y)
        pw, ph = _next_pow2(w), _next_pow2(ph_real)
        page = np.zeros((ph, pw, 4), dtype=np.uint8)
        page[:ph_real, :w] = arr[y:y + ph_real]
        pages.append((y, ph_real, pw, ph, swizzle(page)))
        y += ph_real
    os.makedirs(os.path.dirname(dst_s3t), exist_ok=True)
    with open(dst_s3t, "wb") as f:
        f.write(b"S3T1")
        f.write(struct.pack("<HHHH", w, h, len(pages), 0))
        for (y0, hh, pw, ph, data) in pages:
            f.write(struct.pack("<HHHH", y0, hh, pw, ph))
            f.write(data)
    return w, h


def find_png(content, content_path):
    p = os.path.join(content, *content_path.replace("\\", "/").split("/")) + ".png"
    return p if os.path.isfile(p) else None


# --------------------------------------------------------------------------
# Maps (.tmx from StardewXnbHack)
# --------------------------------------------------------------------------

def _props(elem):
    out = {}
    if elem is None:
        return out
    pe = elem.find("properties")
    if pe is None:
        return out
    for p in pe.findall("property"):
        v = p.get("value")
        if v is None:
            v = p.text or ""
        out[p.get("name")] = v
    return out


def _decode_layer_data(data_el, w, h):
    enc = data_el.get("encoding")
    comp = data_el.get("compression")
    if enc == "csv":
        vals = [int(v) for v in re.split(r"[,\s]+", data_el.text.strip()) if v]
    elif enc == "base64":
        raw = base64.b64decode(data_el.text.strip())
        if comp == "zlib":
            raw = zlib.decompress(raw)
        elif comp == "gzip":
            raw = gzip.decompress(raw)
        elif comp:
            raise ValueError(f"unsupported layer compression {comp}")
        vals = list(struct.unpack(f"<{len(raw) // 4}I", raw))
    elif enc is None:
        vals = [int(t.get("gid", 0)) for t in data_el.findall("tile")]
    else:
        raise ValueError(f"unsupported layer encoding {enc}")
    if data_el.find("chunk") is not None:
        raise ValueError("infinite maps not supported")
    if len(vals) != w * h:
        raise ValueError(f"layer has {len(vals)} cells, expected {w * h}")
    return vals


def _load_tileset(ts_el, map_dir):
    first = int(ts_el.get("firstgid"))
    if ts_el.get("source"):                      # external .tsx
        tsx = os.path.join(map_dir, ts_el.get("source"))
        ts_el = ET.parse(tsx).getroot()
        map_dir = os.path.dirname(tsx)
    img = ts_el.find("image")
    src = img.get("source")
    tw = int(ts_el.get("tilewidth", 16))
    iw = int(img.get("width", 0))
    ih = int(img.get("height", 0))
    cols = int(ts_el.get("columns", iw // tw if tw else 0))
    count = int(ts_el.get("tilecount", (iw // tw) * (ih // tw) if tw else 0))
    tiles = {}
    anims = {}
    for t in ts_el.findall("tile"):
        tid = int(t.get("id"))
        tiles[tid] = _props(t)
        an = t.find("animation")
        if an is not None:
            fr = [(int(f.get("tileid")), int(f.get("duration"))) for f in an.findall("frame")]
            if fr:
                anims[tid] = fr
    return {
        "firstgid": first, "image": src, "image_dir": map_dir,
        "cols": cols, "count": count, "tiles": tiles, "anims": anims,
        "tilewidth": tw,
    }


def _image_content_path(content, image_src, image_dir):
    """Resolve tileset image to a content path like 'Maps/spring_town'."""
    full = os.path.normpath(os.path.join(image_dir, image_src))
    if not full.lower().endswith(".png"):
        full += ".png"
    if not os.path.isfile(full):
        # XnbHack usually writes bare names relative to Maps/
        alt = os.path.join(content, "Maps", os.path.basename(full))
        if os.path.isfile(alt):
            full = alt
        else:
            return None
    rel = os.path.relpath(full, content).replace("\\", "/")
    return rel[:-4]


def convert_map(content, name, out_dir, textures_needed):
    tmx = os.path.join(content, "Maps", name + ".tmx")
    if not os.path.isfile(tmx):
        print(f"  ! map {name}.tmx not found, skipped")
        return False
    root = ET.parse(tmx).getroot()
    w, h = int(root.get("width")), int(root.get("height"))
    tilesets = [_load_tileset(t, os.path.dirname(tmx)) for t in root.findall("tileset")]
    tilesets.sort(key=lambda t: t["firstgid"])

    # Work out texture names, with {S} for seasonal sheets.
    for ts in tilesets:
        cp = _image_content_path(content, ts["image"], ts["image_dir"])
        if cp is None:
            print(f"  ! tilesheet image {ts['image']} not found")
            ts["tex"] = ""
            continue
        folder, base = os.path.split(cp)
        m = re.match(r"^(spring|summer|fall|winter)_(.*)$", base)
        if m:
            variants = [f"{folder}/{s}_{m.group(2)}" for s in SEASONS]
            have = [v for v in variants if find_png(content, v)]
            for v in have:
                textures_needed.add(v)
            if len(have) == 4:
                ts["tex"] = tex_name(f"{folder}/{{S}}_{m.group(2)}")
            else:
                ts["tex"] = tex_name(cp)
        else:
            textures_needed.add(cp)
            ts["tex"] = tex_name(cp)

    def gid_lookup(gid):
        gid &= 0x1FFFFFFF  # drop flip bits
        if gid == 0:
            return None
        for i in range(len(tilesets) - 1, -1, -1):
            if gid >= tilesets[i]["firstgid"]:
                return i, gid - tilesets[i]["firstgid"]
        return None

    layers = {}
    for lay in root.findall("layer"):
        lname = lay.get("name")
        if lname in LAYERS:
            layers[lname] = _decode_layer_data(lay.find("data"), w, h)

    flags = bytearray(w * h)
    ts_arr = {L: bytearray(w * h) for L in LAYERS}
    tile_arr = {L: [0xFFFF] * (w * h) for L in LAYERS}
    for L in LAYERS:
        data = layers.get(L)
        if not data:
            continue
        for i, gid in enumerate(data):
            r = gid_lookup(gid)
            if r is None:
                continue
            tsi, tid = r
            ts_arr[L][i] = tsi
            tile_arr[L][i] = tid
            props = tilesets[tsi]["tiles"].get(tid, {})
            if L == "Back":
                if props.get("Diggable", "").upper().startswith("T"):
                    flags[i] |= F_DIGGABLE
                if props.get("Water", "").upper().startswith("T"):
                    flags[i] |= F_WATER | F_BLOCKED
                if props.get("Passable", "").upper().startswith("F"):
                    flags[i] |= F_BLOCKED
                if "NoSpawn" in props:
                    flags[i] |= F_NOSPAWN
            elif L == "Buildings":
                if "Passable" in props:
                    flags[i] |= F_PASSABLE_OVERRIDE
                else:
                    flags[i] |= F_BLOCKED

    # Per-cell properties (Tiled object layers with TileData / tile objects)
    for og in root.findall("objectgroup"):
        lname = og.get("name", "")
        for ob in og.findall("object"):
            pr = _props(ob)
            if not pr:
                continue
            x = int(float(ob.get("x", 0)) // 16)
            y = int(float(ob.get("y", 0)) // 16)
            if not (0 <= x < w and 0 <= y < h):
                continue
            i = y * w + x
            if "Diggable" in pr:
                flags[i] |= F_DIGGABLE
            if "Water" in pr:
                flags[i] |= F_WATER | F_BLOCKED
            if "NoSpawn" in pr:
                flags[i] |= F_NOSPAWN
            if "Passable" in pr and lname.startswith("Buildings"):
                flags[i] |= F_PASSABLE_OVERRIDE
                flags[i] &= ~F_BLOCKED & 0xFF

    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, name + ".smap")
    with open(path, "wb") as f:
        f.write(b"SMP1")
        f.write(struct.pack("<HHB", w, h, len(tilesets)))
        for ts in tilesets:
            nb = ts["tex"].encode()
            f.write(struct.pack("<B", len(nb)))
            f.write(nb)
            f.write(struct.pack("<HHH", ts["cols"], ts["count"], len(ts["anims"])))
            for tid, frames in sorted(ts["anims"].items()):
                frames = frames[:255]
                f.write(struct.pack("<HBH", tid, len(frames), frames[0][1]))
                f.write(struct.pack(f"<{len(frames)}H", *[fr[0] for fr in frames]))
        f.write(struct.pack("<B", len(LAYERS)))
        for L in LAYERS:
            f.write(struct.pack(f"<{w * h}H", *tile_arr[L]))
            f.write(bytes(ts_arr[L]))
        f.write(bytes(flags))
    nd = sum(1 for b in flags if b & F_DIGGABLE)
    print(f"  map {name}: {w}x{h}, {len(tilesets)} tilesheets, {nd} diggable tiles")
    return True


# --------------------------------------------------------------------------
# Data
# --------------------------------------------------------------------------

def _load_json(content, rel):
    p = os.path.join(content, *rel.split("/"))
    if not os.path.isfile(p):
        print(f"  ! {rel} not found")
        return {}
    with open(p, encoding="utf-8-sig") as f:
        return json.load(f)


def _clean(s):
    return str(s).replace("\t", " ").replace("\n", " ")


def write_data(content, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    season_bits = {"spring": 1, "summer": 2, "fall": 4, "winter": 8}

    crops = _load_json(content, "Data/Crops.json")
    n = 0
    with open(os.path.join(out_dir, "crops.dat"), "w", encoding="utf-8") as f:
        for seed, c in crops.items():
            if not str(seed).isdigit():
                continue
            if c.get("Texture") not in (None, "TileSheets\\crops", "TileSheets/crops"):
                continue
            mask = 0
            for s in c.get("Seasons", []):
                mask |= season_bits.get(str(s).lower(), 0)
            phases = ",".join(str(int(d)) for d in c.get("DaysInPhase", []) if int(d) < 99999)
            harvest = str(c.get("HarvestItemId", "-1")).replace("(O)", "")
            if not harvest.lstrip("-").isdigit():
                continue
            f.write("\t".join(_clean(x) for x in [
                seed, mask, phases or "1", int(c.get("RegrowDays", -1)), harvest,
                int(c.get("SpriteIndex", 0)), 1 if c.get("IsRaised") else 0,
                int(c.get("HarvestMinStack", 1)),
            ]) + "\n")
            n += 1
    print(f"  crops.dat: {n} crops")

    objs = _load_json(content, "Data/Objects.json")
    n = 0
    with open(os.path.join(out_dir, "objects.dat"), "w", encoding="utf-8") as f:
        for oid, o in objs.items():
            if not str(oid).isdigit():
                continue
            if o.get("Texture") not in (None, "", "Maps\\springobjects", "Maps/springobjects"):
                continue
            f.write("\t".join(_clean(x) for x in [
                oid, o.get("Name", oid), int(o.get("Price", 0)),
                int(o.get("SpriteIndex", 0)), int(o.get("Category", 0)),
                int(o.get("Edibility", -300)),
            ]) + "\n")
            n += 1
    print(f"  objects.dat: {n} objects")

    tools = _load_json(content, "Data/Tools.json")
    n = 0
    with open(os.path.join(out_dir, "tools.dat"), "w", encoding="utf-8") as f:
        for key, t in tools.items():
            idx = int(t.get("MenuSpriteIndex", -1))
            if idx < 0:
                idx = int(t.get("SpriteIndex", 0))
            texp = t.get("Texture") or "TileSheets\\tools"
            f.write("\t".join(_clean(x) for x in [
                key, t.get("ClassName", key), t.get("Name", key),
                int(t.get("UpgradeLevel", 0)), idx, tex_name(texp.replace("\\", "/")),
            ]) + "\n")
            n += 1
    print(f"  tools.dat: {n} tools")


DEFAULT_LAYOUT = """\
# stardew3ds farm layout -- editable, no rebuild needed.
# Positions are in tiles. Lines starting with # are ignored.
#
# building <name> <texture> <srcX> <srcY> <srcW> <srcH> <tileX> <tileY> <footW> <footH>
#   Sprite is drawn bottom-aligned to the footprint and centred on it.
#   The footprint blocks movement.
building farmhouse Buildings_houses 0 0 160 144 59 12 9 6
building shippingbin Buildings_Shipping_Bin 0 0 32 32 71 14 2 1
#
# door <tileX> <tileY>        A on this tile (walking into it from below) = go to bed
door 64 17
# spawn <tileX> <tileY>       where you wake up each morning
spawn 64 18
# bin <tileX> <tileY>         tiles that open the shipping bin
bin 71 14
bin 72 14
"""


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    content = sys.argv[1]
    out_root = os.path.join(sys.argv[2], "3ds", "stardew3ds")
    if not os.path.isdir(os.path.join(content, "Maps")):
        sys.exit(f"'{content}' doesn't look like an unpacked Content folder "
                 "(no Maps/ inside). Run StardewXnbHack first.")

    print("Maps...")
    textures = set()
    for m in MAPS:
        convert_map(content, m, os.path.join(out_root, "maps"), textures)

    print("Data...")
    write_data(content, os.path.join(out_root, "data"))

    for cp, required in EXTRA_TEXTURES:
        if find_png(content, cp):
            textures.add(cp)
        elif required:
            print(f"  ! required texture {cp}.png missing")

    print(f"Textures ({len(textures)})...")
    for cp in sorted(textures):
        src = find_png(content, cp)
        if not src:
            print(f"  ! {cp}.png missing")
            continue
        w, h = convert_texture(src, os.path.join(out_root, "tex", tex_name(cp) + ".s3t"))
        print(f"  {cp}  {w}x{h}")

    lay = os.path.join(out_root, "layout.txt")
    if not os.path.exists(lay):
        with open(lay, "w") as f:
            f.write(DEFAULT_LAYOUT)
    with open(os.path.join(out_root, "VERSION"), "w") as f:
        f.write("assets 1\n")
    print(f"\nDone. Copy '{os.path.join(sys.argv[2], '3ds')}' to the root of your SD card.")


if __name__ == "__main__":
    main()
