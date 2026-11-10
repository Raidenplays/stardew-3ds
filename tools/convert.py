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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

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
    ("TileSheets/furniture", False),
    ("Characters/Farmer/shirts", False),
    ("Buildings/houses", False),
    ("Buildings/Shipping Bin", False),
    ("LooseSprites/Cursors", False),
    ("LooseSprites/emotes", False),
    ("LooseSprites/font_bold", False),
]

# Maps are discovered from Maps/*.tmx; these must exist.
REQUIRED_MAPS = ["Farm"]

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
    """Split into pages of at most 1024x1024 (the 3DS GPU limit), each padded
    to power-of-two and swizzled. Writes S3T2 (pages carry x0 and y0)."""
    img = Image.open(src_png).convert("RGBA")
    w, h = img.size
    arr = np.asarray(img)
    pages = []
    for y in range(0, h, MAX_PAGE):
        for x in range(0, w, MAX_PAGE):
            pw_real, ph_real = min(MAX_PAGE, w - x), min(MAX_PAGE, h - y)
            pw, ph = _next_pow2(pw_real), _next_pow2(ph_real)
            page = np.zeros((ph, pw, 4), dtype=np.uint8)
            page[:ph_real, :pw_real] = arr[y:y + ph_real, x:x + pw_real]
            pages.append((x, y, pw_real, ph_real, pw, ph, swizzle(page)))
    os.makedirs(os.path.dirname(dst_s3t), exist_ok=True)
    with open(dst_s3t, "wb") as f:
        f.write(b"S3T2")
        f.write(struct.pack("<HHHH", w, h, len(pages), 0))
        for (x0, y0, ww, hh, pw, ph, data) in pages:
            f.write(struct.pack("<HHHHHH", x0, y0, ww, hh, pw, ph))
            f.write(data)
    return w, h


def farmer_bob(content, out_dir):
    """Per-frame vertical bob of the farmer body (topmost opaque row vs the
    row's standing frame), so shirt and hair move with the body."""
    p = find_png(content, "Characters/Farmer/farmer_base")
    if not p:
        return
    a = np.asarray(Image.open(p).convert("RGBA"))[:, :, 3]
    rows = a.shape[0] // 32
    with open(os.path.join(out_dir, "farmer_bob.dat"), "w") as f:
        for r in range(rows):
            tops = []
            for c in range(6):
                cell = a[r * 32:(r + 1) * 32, c * 16:(c + 1) * 16]
                ys = np.nonzero(cell.max(axis=1))[0]
                tops.append(int(ys[0]) if len(ys) else 0)
            for c in range(6):
                f.write(f"{r * 6 + c}\t{tops[c] - tops[0]}\n")


def write_furniture(content, out_dir):
    furn = _load_json(content, "Data/Furniture.json")
    n = 0
    with open(os.path.join(out_dir, "furniture.dat"), "w", encoding="utf-8") as f:
        for fid, line in furn.items():
            if not str(fid).isdigit():
                continue
            parts = str(line).split("/")
            if len(parts) < 4:
                continue
            def wh(t, dflt):
                try:
                    a, b = t.split()
                    return int(a), int(b)
                except ValueError:
                    return dflt
            sw, sh = wh(parts[2], (1, 2))
            bw, bh = wh(parts[3], (1, 1))
            f.write(f"{fid}\t{_clean(parts[0])}\t{_clean(parts[1])}\t{sw}\t{sh}\t{bw}\t{bh}\n")
            n += 1
    print(f"  furniture.dat: {n} pieces")


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
    image_src = image_src.replace("\\", "/")
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


def convert_map(content, name, out_dir, textures_needed, tmx=None):
    tmx = tmx or os.path.join(content, "Maps", name + ".tmx")
    if not os.path.isfile(tmx):
        print(f"  ! map {name}.tmx not found, skipped")
        return None
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
            lw = int(lay.get("width", w))
            lh = int(lay.get("height", h))
            raw = _decode_layer_data(lay.find("data"), lw, lh)
            # some maps have layers smaller than the map: pad / crop to fit
            vals = [0] * (w * h)
            for yy in range(min(h, lh)):
                for xx in range(min(w, lw)):
                    k = yy * lw + xx
                    if k < len(raw):
                        vals[yy * w + xx] = raw[k]
            layers[lname] = vals

    flags = bytearray(w * h)
    actions = {}          # cell -> action string
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
            if props.get("Action") and L in ("Buildings", "Front", "Back"):
                actions.setdefault(i, props["Action"])
            if L == "Buildings":
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
            if pr.get("Action"):
                # an object can cover several tiles (wide doors, counters)
                ow = max(1, int(float(ob.get("width", 16)) // 16))
                oh = max(1, int(float(ob.get("height", 16)) // 16))
                for yy in range(y, min(h, y + oh)):
                    for xx in range(x, min(w, x + ow)):
                        actions[yy * w + xx] = pr["Action"]

    mprops = _props(root)
    outdoors = mprops.get("Outdoors", "F").upper().startswith("T") or name == "Farm"
    warps = []
    toks = mprops.get("Warp", "").split()
    for k in range(0, len(toks) - 4, 5):
        try:
            warps.append((int(toks[k]), int(toks[k + 1]), toks[k + 2],
                          int(toks[k + 3]), int(toks[k + 4])))
        except ValueError:
            pass

    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, name + ".smap")
    with open(path, "wb") as f:
        f.write(b"SMP2")
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
        f.write(struct.pack("<BH", 1 if outdoors else 0, len(warps)))
        for (x, y, tgt, tx, ty) in warps:
            tb = tgt.encode()[:255]
            f.write(struct.pack("<hhB", x, y, len(tb)))
            f.write(tb)
            f.write(struct.pack("<hh", tx, ty))
        acts = sorted(actions.items())[:65535]
        f.write(struct.pack("<H", len(acts)))
        for cell, a in acts:
            ab = a.encode()[:65535]
            f.write(struct.pack("<HHH", cell & 0xFFFF, cell >> 16, len(ab)))
            f.write(ab)
    nd = sum(1 for b in flags if b & F_DIGGABLE)
    print(f"  map {name}: {w}x{h}, {len(tilesets)} sheets, {len(warps)} warps, "
          f"{len(actions)} actions, {nd} diggable")
    edges = [(x, y, tgt, tx, ty) for (x, y, tgt, tx, ty) in warps]
    for cell, a in actions.items():
        parts = a.split()
        if parts and parts[0] in ("Warp", "LockedDoorWarp", "MagicWarp") and len(parts) >= 4:
            try:
                edges.append((cell % w, cell // w, parts[3], int(parts[1]), int(parts[2])))
            except ValueError:
                pass
    return {"w": w, "h": h, "edges": edges}


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

    write_shops(content, out_dir, season_bits)
    write_location_names(content, out_dir)
    farmer_bob(content, out_dir)
    write_furniture(content, out_dir)

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


def _parse_condition(cond, season_bits):
    """Returns (ok, seasonMask, minYear). ok=False for conditions we can't
    evaluate yet, so the item is left out rather than shown too early."""
    mask, min_year = 0, 1
    if not cond:
        return True, 0, 1
    for clause in [c.strip() for c in cond.split(",") if c.strip()]:
        parts = clause.split()
        neg = parts[0].startswith("!")
        q = parts[0].lstrip("!").upper()
        args = parts[1:]
        if q in ("SEASON", "LOCATION_SEASON"):
            if q == "LOCATION_SEASON":
                args = args[1:]   # first arg is the location
            m = 0
            for a in args:
                m |= season_bits.get(a.lower(), 0)
            if neg:
                m = 0xF & ~m
            mask = (mask & m) if mask else m
        elif q == "YEAR" and args and not neg:
            try:
                min_year = max(min_year, int(args[0]))
            except ValueError:
                return False, 0, 1
        else:
            return False, 0, 1
    return True, mask, min_year


def write_location_names(content, out_dir):
    """Data/Locations DisplayName -> data/location_names.dat (mod places get real names)."""
    p = os.path.join(content, "Data", "Locations.json")
    if not os.path.isfile(p):
        return
    try:
        data = json.load(open(p, encoding="utf-8-sig"))
    except ValueError:
        return
    n = 0
    with open(os.path.join(out_dir, "location_names.dat"), "w", encoding="utf-8") as f:
        for name, d in data.items():
            dn = str((d or {}).get("DisplayName") or "")
            if not dn or dn.startswith("["):
                continue
            f.write(f"{_clean(name)}\t{_clean(dn)}\n")
            n += 1
    print(f"  location_names.dat: {n} names")


def write_shops(content, out_dir, season_bits):
    shops = _load_json(content, "Data/Shops.json")
    n = skipped = 0
    with open(os.path.join(out_dir, "shops.dat"), "w", encoding="utf-8") as f:
        for shop_id, sh in shops.items():
            for it in sh.get("Items", []) or []:
                if it.get("IsRecipe"):
                    continue
                iid = str(it.get("ItemId") or it.get("Id") or "")
                if iid.startswith("(O)"):
                    iid = iid[3:]
                if not iid.isdigit():
                    continue
                ok, mask, min_year = _parse_condition(it.get("Condition"), season_bits)
                if not ok:
                    skipped += 1
                    continue
                price = int(it.get("Price", -1) or -1)
                f.write(f"{_clean(shop_id)}\t{iid}\t{price}\t{mask}\t{min_year}\n")
                n += 1
    print(f"  shops.dat: {n} shop items ({skipped} with conditions not supported yet)")



SEASON_IDX = {"spring": 0, "summer": 1, "fall": 2, "winter": 3}
DIR_IDX = {"down": 0, "right": 1, "up": 2, "left": 3}


def _flat(v):
    return str(v).replace("\t", " ").replace("\r", " ").replace("\n", " ")


def _display(name, raw):
    if not raw or str(raw).startswith("["):
        return name    # localisation token -> use the internal name
    return str(raw)


def _dir(v):
    if isinstance(v, int):
        # Stardew numbers: 0 up, 1 right, 2 down, 3 left -> ours: 0 down 1 right 2 up 3 left
        return {0: 2, 1: 1, 2: 0, 3: 3}.get(v, 0)
    return DIR_IDX.get(str(v).lower(), 0)


def _load_npc_defs(content):
    """Returns {name: dict(display, bseason, bday, home, hx, hy, hdir, social)}."""
    out = {}
    chars = _load_json(content, "Data/Characters.json") if os.path.isfile(
        os.path.join(content, "Data", "Characters.json")) else {}
    for name, c in chars.items():
        home = None
        for h in c.get("Home") or []:
            if h.get("Location") and h.get("Tile") is not None:
                home = h
                break
        if not home:
            continue
        t = home["Tile"]
        hx, hy = (t.get("X", 0), t.get("Y", 0)) if isinstance(t, dict) else (0, 0)
        cs = str(c.get("CanSocialize") or "TRUE").strip().upper()
        out[name] = dict(display=_display(name, c.get("DisplayName")),
                         bseason=SEASON_IDX.get(str(c.get("BirthSeason") or "").lower(), -1),
                         bday=int(c.get("BirthDay") or 0), home=home["Location"], hx=int(hx), hy=int(hy),
                         hdir=_dir(home.get("Direction", "down")), social=0 if cs == "FALSE" else 1)
    if out:
        return out
    # pre-1.6 fallback: Data/NPCDispositions
    disp = _load_json(content, "Data/NPCDispositions.json") if os.path.isfile(
        os.path.join(content, "Data", "NPCDispositions.json")) else {}
    for name, line in disp.items():
        f = str(line).split("/")
        if len(f) < 11:
            continue
        bs, bd = (f[8].split() + ["", "0"])[:2]
        hp = f[10].split()
        if len(hp) < 3:
            continue
        out[name] = dict(display=f[11] if len(f) > 11 and f[11] else name,
                         bseason=SEASON_IDX.get(bs.lower(), -1), bday=int(bd or 0),
                         home=hp[0], hx=int(hp[1]), hy=int(hp[2]), hdir=0, social=1)
    return out


def write_events(content, out_dir):
    """Data/Events/<Location>.json -> data/events/<Location>.dat (base language only)."""
    src = os.path.join(content, "Data", "Events")
    if not os.path.isdir(src):
        print("  no Data/Events folder")
        return
    os.makedirs(os.path.join(out_dir, "events"), exist_ok=True)
    files = n = 0
    for fn in sorted(os.listdir(src)):
        if not fn.endswith(".json") or re.search(r"\.[a-z]{2}-[A-Z]{2}\.json$", fn):
            continue
        try:
            ev = json.load(open(os.path.join(src, fn), encoding="utf-8-sig"))
        except ValueError as e:
            print(f"  ! {fn}: {e}")
            continue
        with open(os.path.join(out_dir, "events", fn[:-5] + ".dat"), "w", encoding="utf-8") as f:
            for k, v in ev.items():
                f.write(f"{_flat(k)}\t{_flat(v)}\n")
                n += 1
        files += 1
    print(f"  {n} events in {files} locations")


def write_npcs(content, out_dir, textures):
    defs = _load_npc_defs(content)
    tastes = _load_json(content, "Data/NPCGiftTastes.json") if os.path.isfile(
        os.path.join(content, "Data", "NPCGiftTastes.json")) else {}
    os.makedirs(os.path.join(out_dir, "schedules"), exist_ok=True)
    os.makedirs(os.path.join(out_dir, "dialogue"), exist_ok=True)

    def ids(txt):
        return " ".join(t.replace("(O)", "") for t in str(txt).split()
                        if t.replace("(O)", "").lstrip("-").isdigit())

    with open(os.path.join(out_dir, "universal_tastes.dat"), "w", encoding="utf-8") as f:
        for i, key in enumerate(("Universal_Love", "Universal_Like", "Universal_Dislike",
                                 "Universal_Hate", "Universal_Neutral")):
            f.write(f"{i}\t{ids(tastes.get(key, ''))}\n")

    rainy_path = os.path.join(content, "Characters", "Dialogue", "rainy.json")
    rainy = json.load(open(rainy_path, encoding="utf-8-sig")) if os.path.isfile(rainy_path) else {}
    n_sched = n_dia = n_spr = 0
    with open(os.path.join(out_dir, "npcs.dat"), "w", encoding="utf-8") as f:
        for name, d in sorted(defs.items()):
            parts = str(tastes.get(name, "")).split("/")
            parts += [""] * (10 - len(parts))
            texts = [_flat(parts[i]) for i in (0, 2, 4, 6, 8)]
            lists = [ids(parts[i]) for i in (1, 3, 5, 7, 9)]
            f.write("\t".join([name, _flat(d["display"]), str(d["bseason"]), str(d["bday"]), d["home"],
                               str(d["hx"]), str(d["hy"]), str(d["hdir"]), str(d["social"])]
                              + lists + texts) + "\n")
            sp = os.path.join(content, "Characters", "schedules", name + ".json")
            if not os.path.isfile(sp):
                sp = os.path.join(content, "Characters", "Schedules", name + ".json")
            if os.path.isfile(sp):
                sched = json.load(open(sp, encoding="utf-8-sig"))
                with open(os.path.join(out_dir, "schedules", name + ".dat"), "w", encoding="utf-8") as g:
                    for k, v in sched.items():
                        g.write(f"{_flat(k)}\t{_flat(v)}\n")
                n_sched += 1
            dp = os.path.join(content, "Characters", "Dialogue", name + ".json")
            lines = {}
            if os.path.isfile(dp):
                lines = json.load(open(dp, encoding="utf-8-sig"))
            if name in rainy:
                lines["__rain"] = rainy[name]
            if lines:
                with open(os.path.join(out_dir, "dialogue", name + ".dat"), "w", encoding="utf-8") as g:
                    for k, v in lines.items():
                        g.write(f"{_flat(k)}\t{_flat(v)}\n")
                n_dia += 1
            for folder in ("Characters", "Portraits"):
                if find_png(content, f"{folder}/{name}"):
                    textures.add(f"{folder}/{name}")
                    n_spr += 1
    print(f"  {len(defs)} villagers, {n_sched} schedules, {n_dia} dialogue files, {n_spr} sprites/portraits")


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
#
# bed <map> <tileX> <tileY> <w> <h>   A on these tiles = sleep
bed FarmHouse 9 5 2 3
#
# furniture <map> <id> <tileX> <tileY>   id from Data/Furniture.json; tile = top-left of
#                                        its footprint. 2048 is the starter bed.
furniture FarmHouse 2048 9 5
# wake <map> <tileX> <tileY>          where you wake up (nearest open tile is used)
wake FarmHouse 9 8
#
# farmer <name>                       what villagers call you
farmer Farmer
#
# outfit <shirt> <pants> <pantsR> <pantsG> <pantsB> <hair> <hairR> <hairG> <hairB>
#   shirt / pants = SpriteIndex from Data/Shirts.json / Data/Pants.json
outfit 0 0 46 85 183 0 138 78 38
"""


def location_maps(content):
    """Location name -> .tmx path. Base maps by file name, plus locations that
    Data/Locations creates from another map asset (how 1.6 mods add places)."""
    maps_dir = os.path.join(content, "Maps")
    out = {f[:-4]: os.path.join(maps_dir, f) for f in os.listdir(maps_dir) if f.lower().endswith(".tmx")}
    locs = os.path.join(content, "Data", "Locations.json")
    if os.path.isfile(locs):
        try:
            data = json.load(open(locs, encoding="utf-8-sig"))
        except ValueError:
            data = {}
        for name, d in data.items():
            col = (d or {}).get("CreateOnLoad") or {}
            mp = col.get("MapPath")
            if not mp:
                continue
            p = os.path.join(content, *str(mp).replace("\\", "/").split("/")) + ".tmx"
            if os.path.isfile(p):
                out[name] = p
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    opts = {a.split("=")[0]: (a.split("=", 1)[1] if "=" in a else True) for a in sys.argv[1:] if a.startswith("--")}
    if len(args) < 2:
        print(__doc__)
        print("Options: --mods=<folder with Content Patcher packs, e.g. SVE / Ridgeside>")
        sys.exit(1)
    content = args[0]
    out_root = os.path.join(args[1], "3ds", "stardew3ds")
    if opts.get("--mods") and opts["--mods"] is not True:
        import cp
        merged = os.path.join(args[1], "_merged_content")
        print("Mods...")
        cp.apply_all(content, opts["--mods"], merged)
        content = merged
    if not os.path.isdir(os.path.join(content, "Maps")):
        sys.exit(f"'{content}' doesn't look like an unpacked Content folder "
                 "(no Maps/ inside). Run StardewXnbHack first.")

    print("Maps...")
    textures = set()
    locs = location_maps(content)
    names = sorted(locs)
    for m in REQUIRED_MAPS:
        if m not in names:
            sys.exit(f"Maps/{m}.tmx missing -- is this really an unpacked Content folder?")
    ok = 0
    graph = {}
    for m in names:
        try:
            info = convert_map(content, m, os.path.join(out_root, "maps"), textures, locs[m])
            if info:
                ok += 1
                graph[m] = info
        except Exception as e:   # one odd map shouldn't stop the rest
            print(f"  ! {m}: {e}")
    print(f"  {ok}/{len(names)} maps converted")
    os.makedirs(os.path.join(out_root, "data"), exist_ok=True)
    with open(os.path.join(out_root, "data", "graph.dat"), "w", encoding="utf-8") as f:
        for m, info in sorted(graph.items()):
            for (x, y, tgt, tx, ty) in info["edges"]:
                if tgt in graph:   # only routes to maps we actually have
                    f.write(f"{m}\t{x}\t{y}\t{tgt}\t{tx}\t{ty}\n")

    print("Data...")
    write_data(content, os.path.join(out_root, "data"))
    print("Villagers...")
    write_npcs(content, os.path.join(out_root, "data"), textures)
    print("Events...")
    write_events(content, os.path.join(out_root, "data"))

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
