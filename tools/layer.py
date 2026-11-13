#!/usr/bin/env python3
"""
Turns a modded conversion into a mod layer for the 3DS mod loader.

    python3 layer.py <base root> <modded root> <out layer dir> --id=<id> [--name=..] [--version=..]

<base root> and <modded root> are both ".../3ds/stardew3ds" folders made by
convert.py (without and with --mods). The layer keeps only what differs:
  - maps/*.smap, tex/*.s3t ... that are new or changed: copied as-is
  - data/**/*.dat tables: written as deltas ("#delta", "+\t<line>", "-\t<line>")
and a mod.txt describing the layer.
"""
import collections
import filecmp
import os
import shutil
import sys

SKIP = {"layout.txt", "VERSION", "mods.txt"}


def _lines(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return [l.rstrip("\r\n") for l in f]


def table_delta(base_path, mod_path):
    base = _lines(base_path) if base_path and os.path.isfile(base_path) else []
    mod = _lines(mod_path)
    b, m = collections.Counter(base), collections.Counter(mod)
    removed = list((b - m).elements())
    added = [l for l in mod if (m - b)[l] > 0]
    # keep order and multiplicity for additions
    want = m - b
    out_add = []
    for l in mod:
        if want[l] > 0:
            out_add.append(l)
            want[l] -= 1
    return removed, out_add


def build_layer(base_root, mod_root, out_dir, info):
    if os.path.exists(out_dir):
        shutil.rmtree(out_dir)
    os.makedirs(out_dir)
    stats = collections.Counter()
    for dirpath, dirs, files in os.walk(mod_root):
        rel_dir = os.path.relpath(dirpath, mod_root)
        if rel_dir.split(os.sep)[0] == "mods":
            continue
        for fn in files:
            rel = os.path.normpath(os.path.join(rel_dir, fn)).replace("\\", "/")
            if rel in SKIP:
                continue
            src = os.path.join(mod_root, rel)
            bpath = os.path.join(base_root, rel)
            dst = os.path.join(out_dir, rel)
            if rel.startswith("data/") and rel.endswith(".dat"):
                removed, added = table_delta(bpath, src)
                if not removed and not added:
                    continue
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                with open(dst, "w", encoding="utf-8") as f:
                    f.write("#delta\n")
                    for l in removed:
                        f.write("-\t" + l + "\n")
                    for l in added:
                        f.write("+\t" + l + "\n")
                stats["tables"] += 1
                stats["lines"] += len(removed) + len(added)
            else:
                if os.path.isfile(bpath) and filecmp.cmp(bpath, src, shallow=False):
                    continue
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copyfile(src, dst)
                stats[rel.split("/")[0]] += 1
    with open(os.path.join(out_dir, "mod.txt"), "w", encoding="utf-8") as f:
        for k in ("name", "version", "author", "description"):
            if info.get(k):
                f.write(f"{k}: {info[k]}\n")
        if info.get("includes"):
            f.write(f"includes: {info['includes']}\n")
    return stats


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    opts = dict(a[2:].split("=", 1) for a in sys.argv[1:] if a.startswith("--") and "=" in a)
    if len(args) < 3 or "id" not in opts:
        print(__doc__)
        sys.exit(1)
    info = {"name": opts.get("name", opts["id"]), "version": opts.get("version", "1.0"),
            "author": opts.get("author", ""), "description": opts.get("description", "")}
    st = build_layer(args[0], args[1], args[2], info)
    print("layer:", ", ".join(f"{k} {v}" for k, v in st.items()) or "no changes")
