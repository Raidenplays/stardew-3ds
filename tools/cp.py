#!/usr/bin/env python3
"""
Applies Content Patcher packs (and their i18n, config and dynamic tokens) to
an unpacked Content folder ahead of time, producing a merged Content folder
the converter then turns into 3DS files.

It evaluates patches for a day-one baseline (spring 1, year 1, sunny,
nothing seen yet, the packs' default config). Things that change later in
play (hearts, events seen, seasons) are left to the game's own systems or a
later per-season pass.

Usage (normally called by convert.py --mods):
    python3 cp.py "<Content (unpacked)>" <mods folder> <merged out folder>
"""
import copy
import json
import os
import re
import shutil
import sys
from struct import error as struct_error

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sjson  # noqa: E402
import tmxmap  # noqa: E402

ASSET_EXTS = (".tmx", ".tbin", ".png", ".json", ".xnb", ".fnt", ".xml")


def norm_asset(name):
    n = str(name).strip().replace("\\", "/")
    while "//" in n:
        n = n.replace("//", "/")
    root, ext = os.path.splitext(n)
    if ext.lower() in ASSET_EXTS:
        n = root
    return n.strip("/")


# ============================================================ merged content
class Content:
    """The merged Content folder. Base files are hard-linked (no copy cost);
    anything a mod changes is rewritten in place."""

    def __init__(self, base, out):
        self.base, self.out = base, out
        self.index = {}          # lower asset name -> real path in out
        self.maps = {}           # lower asset -> tmxmap.Map (edited, unsaved)
        self.data = {}           # lower asset -> parsed json (edited, unsaved)
        self.images = {}         # lower asset -> PIL image (edited, unsaved)
        self.sheet_dirs = {}     # map asset -> folder its own tilesheets live in
        self.names = {}          # lower asset -> asset name as first written

    def build(self):
        if os.path.exists(self.out):
            shutil.rmtree(self.out)
        for dirpath, dirs, files in os.walk(self.base):
            rel = os.path.relpath(dirpath, self.base)
            dst = os.path.join(self.out, rel)
            os.makedirs(dst, exist_ok=True)
            for f in files:
                s, d = os.path.join(dirpath, f), os.path.join(dst, f)
                try:
                    os.link(s, d)
                except OSError:
                    shutil.copy2(s, d)
                root, ext = os.path.splitext(f)
                if ext.lower() in ASSET_EXTS:
                    a = norm_asset(os.path.join(rel, root) if rel != "." else root)
                    self.index[a.lower()] = d

    def _ci_join(self, asset):
        """Join asset under out, reusing existing folder names whatever their case."""
        cur = self.out
        parts = asset.split("/")
        for p in parts[:-1]:
            real = None
            if os.path.isdir(cur):
                for n in os.listdir(cur):
                    if n.lower() == p.lower() and os.path.isdir(os.path.join(cur, n)):
                        real = n
                        break
            cur = os.path.join(cur, real or p)
        return os.path.join(cur, parts[-1])

    def path(self, asset):
        return self.index.get(norm_asset(asset).lower())

    def exists(self, asset):
        a = norm_asset(asset).lower()
        return a in self.index or a in self.maps or a in self.data or a in self.images

    def _dest(self, asset, ext):
        a = norm_asset(asset)
        old = self.index.get(a.lower())
        if old and os.path.splitext(old)[1].lower() == ext:
            d = old                      # keep the original folder/file capitalisation
        else:
            a = self.names.get(a.lower(), a)
            d = self._ci_join(a) + ext
        if old and old != d and os.path.exists(old):
            os.remove(old)
        os.makedirs(os.path.dirname(d), exist_ok=True)
        if os.path.exists(d):
            os.remove(d)      # break the hard link before writing
        self.index[a.lower()] = d
        return d

    # ---- loads
    def load_file(self, asset, src, src_dir_for_sheets=None):
        self.names.setdefault(norm_asset(asset).lower(), norm_asset(asset))
        a = norm_asset(asset).lower()
        ext = os.path.splitext(src)[1].lower()
        self.maps.pop(a, None)
        self.data.pop(a, None)
        self.images.pop(a, None)
        if ext in (".tmx", ".tbin"):
            m = tmxmap.read_map(src)
            for ts in m.tilesets:
                ts._src_dir = os.path.dirname(src)
            self.maps[a] = m
            self.index[a] = self._dest(asset, ".tmx")
        elif ext == ".json":
            self.data[a] = sjson.load(src)
            self.index[a] = self._dest(asset, ".json")
        elif ext == ".xnb":
            import xnb
            try:
                self.data[a] = xnb.read_dict(src)
            except (ValueError, IndexError, struct_error):
                return False
            self.index[a] = self._dest(asset, ".json")
        elif ext == ".png":
            d = self._dest(asset, ".png")
            shutil.copyfile(src, d)
        else:
            return False   # .xnb etc: not supported yet
        return True

    # ---- accessors that keep the edited copy in memory
    def get_map(self, asset):
        a = norm_asset(asset).lower()
        if a not in self.maps:
            p = self.index.get(a)
            if not p or not p.lower().endswith((".tmx", ".tbin")):
                return None
            m = tmxmap.read_map(p)
            for ts in m.tilesets:
                if getattr(ts, "_src_dir", None) is None:
                    ts._src_dir = os.path.dirname(p)
            self.maps[a] = m
        return self.maps[a]

    def get_data(self, asset):
        a = norm_asset(asset).lower()
        if a not in self.data:
            p = self.index.get(a)
            if not p or not p.lower().endswith(".json"):
                return None
            self.data[a] = sjson.load(p)
        return self.data[a]

    def get_image(self, asset):
        a = norm_asset(asset).lower()
        if a not in self.images:
            p = self.index.get(a)
            if not p or not p.lower().endswith(".png"):
                return None
            self.images[a] = Image.open(p).convert("RGBA")
        return self.images[a]

    # ---- write everything that was edited
    def flush(self):
        maps_dir = os.path.join(self.out, "Maps")
        for a, m in self.maps.items():
            dest = self._dest(a, ".tmx")

            def image_for(ts, dest=dest):
                # Prefer a game/mod asset by that name; else copy the sheet next to the map.
                img = ts.image.replace("\\", "/")
                stem = os.path.splitext(img)[0]
                for cand in (stem, "Maps/" + os.path.basename(stem)):
                    p = self.index.get(norm_asset(cand).lower())
                    if p and p.lower().endswith(".png"):
                        return os.path.relpath(p, os.path.dirname(dest)).replace("\\", "/")
                src_dir = getattr(ts, "_src_dir", None)
                if src_dir:
                    real = sjson.ci_path(src_dir, img if img.lower().endswith(".png") else img + ".png")
                    if real:
                        os.makedirs(maps_dir, exist_ok=True)
                        tgt = os.path.join(maps_dir, os.path.basename(real))
                        if not os.path.exists(tgt):
                            shutil.copyfile(real, tgt)
                        self.index.setdefault(norm_asset("Maps/" + os.path.splitext(os.path.basename(real))[0]).lower(), tgt)
                        return os.path.relpath(tgt, os.path.dirname(dest)).replace("\\", "/")
                return img if img.lower().endswith(".png") else img + ".png"

            m.write_tmx(dest, image_for)
        for a, d in self.data.items():
            with open(self._dest(a, ".json"), "w", encoding="utf-8") as f:
                json.dump(d, f, ensure_ascii=False, indent=1)
        for a, im in self.images.items():
            im.save(self._dest(a, ".png"))


# ============================================================ tokens
BASELINE = {
    "season": "spring", "day": "1", "dayofweek": "Monday", "year": "1", "weather": "Sun",
    "time": "0600", "language": "en", "farmtype": "Standard", "isoutdoors": "true",
    "ismainplayer": "true", "isoddyear": "true", "farmhouseupgrade": "0", "farmname": "Farm",
    "daysplayed": "1", "dayevent": "", "spouse": "", "roommate": "", "locationcontext": "Default",
    "playername": "Farmer", "playergender": "Male",
}
FALSE_SET_TOKENS = {"hasseenevent", "hasflag", "hasconversationtopic", "hasdialogueanswer",
                    "relationship", "hearts", "haswalletitem", "hasprofession", "hasactivequest",
                    "hascookingrecipe", "hascraftingrecipe", "haslocation", "skilllevel"}


class Pack:
    def __init__(self, folder, installed_ids):
        self.folder = folder
        self.manifest = sjson.load(os.path.join(folder, "manifest.json"))
        self.id = self.manifest.get("UniqueID", os.path.basename(folder))
        self.content = sjson.load(os.path.join(folder, "content.json"))
        self.installed = installed_ids
        self.i18n = {}
        p = sjson.ci_path(folder, "i18n/default.json")
        if p:
            try:
                self.i18n = {k.lower(): v for k, v in sjson.load(p).items()}
            except ValueError:
                pass
        self.config = {}
        schema = self.content.get("ConfigSchema") or {}
        user = {}
        cp = os.path.join(folder, "config.json")
        if os.path.isfile(cp):
            try:
                user = sjson.load(cp)
            except ValueError:
                user = {}
        for k, v in schema.items():
            val = user.get(k, (v or {}).get("Default", ""))
            self.config[k.lower()] = str(val)
        self.dynamic = {}
        self.warnings = []

    # -------- token evaluation
    def token(self, name, arg, opts, target=None):
        n = name.strip()
        low = n.lower()
        if low == "i18n":
            v = self.i18n.get(arg.strip().lower(), "")
            for k, pv in opts.items():
                v = re.sub(r"\{\{\s*" + re.escape(k) + r"\s*\}\}", pv, v, flags=re.I)
            return v
        if low in ("modid",):
            return self.id
        if low == "target" and target:
            return target
        if low == "targetwithoutpath" and target:
            return target.replace("\\", "/").split("/")[-1]
        if low == "targetpathonly" and target:
            return "/".join(target.replace("\\", "/").split("/")[:-1])
        if low == "lowercase":
            return arg.lower()
        if low == "uppercase":
            return arg.upper()
        if low == "random":
            vals = [v.strip() for v in arg.split(",") if v.strip()]
            return vals[0] if vals else ""
        if low == "range":
            try:
                a, b = [int(float(x)) for x in arg.split(",")[:2]]
                return ", ".join(str(i) for i in range(a, b + 1))
            except ValueError:
                return ""
        if low == "absolutefilepath":
            return os.path.join(self.folder, arg)
        if low == "firstvalidfile":
            for f in arg.split(","):
                if sjson.ci_path(self.folder, f.strip()):
                    return f.strip()
            return ""
        if low == "hasmod":
            return ", ".join(sorted(self.installed))
        if low == "hasvalue":
            return "true" if arg.strip() else "false"
        if low == "hasfile":
            return "true" if sjson.ci_path(self.folder, arg.strip()) else "false"
        if low in self.dynamic:
            return self.dynamic[low]
        if low in self.config:
            return self.config[low]
        if low in BASELINE:
            return BASELINE[low]
        if low in FALSE_SET_TOKENS:
            return ""
        if low == "query":
            return _query(arg)
        return None   # unknown

    def render(self, s, target=None):
        """Replace {{tokens}} (innermost first). Unknown tokens become ''."""
        if not isinstance(s, str) or "{{" not in s:
            return s
        for _ in range(12):
            m = re.search(r"\{\{([^{}]*)\}\}", s)
            if not m:
                break
            s = s[:m.start()] + self._eval_token_expr(m.group(1), target) + s[m.end():]
        return s

    def _eval_token_expr(self, expr, target):
        parts = expr.split("|")
        head = parts[0]
        opts = {}
        for o in parts[1:]:
            if "=" in o:
                k, v = o.split("=", 1)
                opts[k.strip().lower()] = v.strip()
        if ":" in head:
            name, arg = head.split(":", 1)
        else:
            name, arg = head, ""
        val = self.token(name, arg, opts if name.strip().lower() == "i18n" else {}, target)
        if val is None:
            val = ""
        if "contains" in opts:
            want = {x.strip().lower() for x in opts["contains"].split(",")}
            have = {x.strip().lower() for x in val.split(",") if x.strip()}
            val = "true" if want & have else "false"
        return val

    def render_deep(self, obj, target=None):
        if isinstance(obj, str):
            return self.render(obj, target)
        if isinstance(obj, list):
            return [self.render_deep(x, target) for x in obj]
        if isinstance(obj, dict):
            return {self.render(k, target): self.render_deep(v, target) for k, v in obj.items()}
        return obj

    def when_ok(self, when, target=None):
        for k, v in (when or {}).items():
            key = self.render(k, target).strip()
            expect = {x.strip().lower() for x in str(self.render(str(v), target)).split(",") if x.strip()}
            # "Token |contains=X" or "Token: input"
            if "|" in key or ":" in key:
                actual = self._eval_token_expr(key, target)
                name = re.split(r"[:|]", key)[0].strip().lower()
            else:
                name = key.lower()
                actual = self.token(key, "", {}, target)
                if actual is None:
                    # another mod's token ("Some.Mod/Token") or one we don't know
                    actual = ""
            have = {x.strip().lower() for x in str(actual).split(",") if x.strip()}
            if not have and name in FALSE_SET_TOKENS:
                have = set()
            if not (expect & have):
                if not have and expect <= {"false", ""}:
                    continue
                return False
        return True

    def resolve_dynamic(self):
        for dt in self.content.get("DynamicTokens") or []:
            name = str(dt.get("Name", "")).lower()
            if name and self.when_ok(dt.get("When")):
                self.dynamic[name] = self.render(str(dt.get("Value", "")))


def _query(expr):
    e = expr.strip()
    try:
        if re.fullmatch(r"[0-9\s<>=!+\-*/().andornt]+", e.replace("AND", "and").replace("OR", "or")):
            return "true" if eval(e.replace("AND", "and").replace("OR", "or").replace("=", "==")
                                  .replace("<==", "<=").replace(">==", ">=").replace("!==", "!="),
                                  {"__builtins__": {}}) else "false"
    except Exception:
        pass
    return "false"


# ============================================================ patch actions
def _area(a):
    if not a:
        return None
    return (int(a.get("X", 0)), int(a.get("Y", 0)), int(a.get("Width", 0)), int(a.get("Height", 0)))


def collect_changes(pack, changes, base_dir, out, depth=0):
    for ch in changes or []:
        if not isinstance(ch, dict) or not pack.when_ok(ch.get("When")):
            continue
        if str(ch.get("Action", "")).lower() == "include":
            for f in pack.render(str(ch.get("FromFile", ""))).split(","):
                f = f.strip()
                p = sjson.ci_path(pack.folder, f)
                if not p:
                    pack.warnings.append(f"include not found: {f}")
                    continue
                try:
                    inc = sjson.load(p)
                except ValueError as e:
                    pack.warnings.append(f"bad json {f}: {e}")
                    continue
                collect_changes(pack, inc.get("Changes"), base_dir, out, depth + 1)
        else:
            out.append(ch)


def _targets(pack, ch):
    return [t.strip() for t in pack.render(str(ch.get("Target", ""))).split(",") if t.strip()]


def apply_load(pack, c, ch, stats):
    for tgt in _targets(pack, ch):
        src_rel = pack.render(str(ch.get("FromFile", "")), tgt).strip()
        src = sjson.ci_path(pack.folder, src_rel)
        if not src:
            stats["missing"] += 1
            continue
        if c.load_file(tgt, src):
            stats["load"] += 1
        else:
            stats["skipped_xnb"] += 1


def _entry_key_match(container, key):
    if isinstance(container, dict):
        return key
    if isinstance(container, list):
        for i, it in enumerate(container):
            if isinstance(it, dict) and str(it.get("Id", it.get("ID", ""))) == str(key):
                return i
    return None


def _navigate(data, path):
    cur = data
    for p in path:
        if isinstance(cur, dict):
            if p not in cur or cur[p] is None:
                cur[p] = {}
            cur = cur[p]
        elif isinstance(cur, list):
            idx = _entry_key_match(cur, p)
            if idx is None:
                try:
                    idx = int(p)
                except ValueError:
                    return None
            if idx >= len(cur):
                return None
            cur = cur[idx]
        else:
            return None
    return cur


def _text_op(old, op):
    kind = str(op.get("Operation", "")).lower()
    val = str(op.get("Value", ""))
    delim = op.get("Delimiter", " ") if "Delimiter" in op else " "
    old = "" if old is None else str(old)
    if kind == "append":
        return old + (delim if old and val else "") + val
    if kind == "prepend":
        return val + (delim if old and val else "") + old
    if kind == "removedelimited":
        return delim.join(x for x in old.split(delim) if x != op.get("Search", val))
    if kind == "replacedelimited":
        return delim.join(val if x == op.get("Search") else x for x in old.split(delim))
    return old


def apply_editdata(pack, c, ch, stats):
    for tgt in _targets(pack, ch):
        data = c.get_data(tgt)
        if data is None:
            stats["missing_target"] += 1
            continue
        block = pack.render_deep(copy.deepcopy(ch), tgt)
        cont = data
        if block.get("TargetField"):
            cont = _navigate(data, [str(x) for x in block["TargetField"]])
            if cont is None:
                stats["bad_field"] += 1
                continue
        for k, v in (block.get("Entries") or {}).items():
            if isinstance(cont, dict):
                if v is None:
                    cont.pop(k, None)
                else:
                    cont[k] = v
            elif isinstance(cont, list):
                idx = _entry_key_match(cont, k)
                if v is None:
                    if idx is not None:
                        cont.pop(idx)
                elif idx is None:
                    cont.append(v)
                else:
                    cont[idx] = v
            stats["entries"] += 1
        for k, fields in (block.get("Fields") or {}).items():
            idx = _entry_key_match(cont, k) if isinstance(cont, list) else k
            if isinstance(cont, dict) and k not in cont:
                continue
            if idx is None:
                continue
            entry = cont[idx]
            if isinstance(entry, str):
                parts = entry.split("/")
                for fk, fv in fields.items():
                    i = int(fk)
                    while len(parts) <= i:
                        parts.append("")
                    parts[i] = "" if fv is None else str(fv)
                cont[idx] = "/".join(parts)
            elif isinstance(entry, dict):
                for fk, fv in fields.items():
                    if fv is None:
                        entry.pop(fk, None)
                    else:
                        entry[fk] = fv
            stats["fields"] += 1
        for op in block.get("TextOperations") or []:
            path = [str(x) for x in (op.get("Target") or [])]
            if not path:
                continue
            if path[0] in ("Entries", "Fields"):
                path = path[1:]
            if not path:
                continue
            holder = _navigate(cont, path[:-1]) if len(path) > 1 else cont
            last = path[-1]
            if isinstance(holder, dict):
                holder[last] = _text_op(holder.get(last), op)
            elif isinstance(holder, str) and len(path) > 1:
                # field index inside a slash-delimited entry
                parent = _navigate(cont, path[:-2]) if len(path) > 2 else cont
                key = path[-2]
                parts = holder.split("/")
                i = int(last)
                while len(parts) <= i:
                    parts.append("")
                parts[i] = _text_op(parts[i], op)
                if isinstance(parent, dict):
                    parent[key] = "/".join(parts)
            stats["textops"] += 1


def apply_editimage(pack, c, ch, stats):
    for tgt in _targets(pack, ch):
        img = c.get_image(tgt)
        src_rel = pack.render(str(ch.get("FromFile", "")), tgt).strip()
        src = sjson.ci_path(pack.folder, src_rel)
        if img is None or not src:
            stats["missing_target"] += 1
            continue
        patch = Image.open(src).convert("RGBA")
        fa = _area(ch.get("FromArea")) or (0, 0, patch.width, patch.height)
        ta = _area(ch.get("ToArea")) or (0, 0, fa[2], fa[3])
        piece = patch.crop((fa[0], fa[1], fa[0] + fa[2], fa[1] + fa[3]))
        need_w, need_h = max(img.width, ta[0] + ta[2]), max(img.height, ta[1] + ta[3])
        if need_w > img.width or need_h > img.height:
            bigger = Image.new("RGBA", (need_w, need_h), (0, 0, 0, 0))
            bigger.paste(img, (0, 0))
            img = bigger
            c.images[norm_asset(tgt).lower()] = img
        if str(ch.get("PatchMode", "Replace")).lower() == "overlay":
            img.alpha_composite(piece, (ta[0], ta[1]))
        else:
            img.paste(piece, (ta[0], ta[1]))
        stats["images"] += 1


def apply_editmap(pack, c, ch, stats):
    for tgt in _targets(pack, ch):
        m = c.get_map(tgt)
        if m is None:
            stats["missing_target"] += 1
            continue
        block = pack.render_deep(copy.deepcopy(ch), tgt)
        if block.get("FromFile"):
            src = sjson.ci_path(pack.folder, block["FromFile"].strip())
            if not src:
                stats["missing"] += 1
            else:
                try:
                    pm = tmxmap.read_map(src)
                    for ts in pm.tilesets:
                        ts._src_dir = os.path.dirname(src)
                    m.patch_area(pm, _area(block.get("FromArea")), _area(block.get("ToArea")),
                                 block.get("PatchMode") or block.get("Patchmode") or "ReplaceByLayer")
                    stats["map_areas"] += 1
                except Exception as e:   # one broken patch shouldn't stop the run
                    pack.warnings.append(f"map patch {block['FromFile']}: {e}")
        for k, v in (block.get("MapProperties") or {}).items():
            if v is None:
                m.properties.pop(k, None)
            else:
                m.properties[k] = str(v)
        for t in block.get("MapTiles") or []:
            pos = t.get("Position") or {}
            m.set_tile(t.get("Layer", "Back"), int(pos.get("X", 0)), int(pos.get("Y", 0)),
                       t.get("SetTilesheet"), t.get("SetIndex"), t.get("SetProperties"),
                       bool(t.get("Remove")))
            stats["map_tiles"] += 1
        for w in block.get("AddWarps") or []:
            m.properties["Warp"] = (m.properties.get("Warp", "") + " " + str(w)).strip()
        for op in block.get("TextOperations") or []:
            path = [str(x) for x in (op.get("Target") or [])]
            if len(path) == 2 and path[0] == "MapProperties":
                m.properties[path[1]] = _text_op(m.properties.get(path[1]), op)
        stats["maps"] += 1


# ============================================================ driver
def find_packs(mods_dir):
    packs = []
    for dirpath, dirs, files in os.walk(mods_dir):
        if "manifest.json" in files and "content.json" in files:
            try:
                man = sjson.load(os.path.join(dirpath, "manifest.json"))
            except ValueError:
                continue
            if "contentpatcher" in str(man.get("ContentPackFor", {}).get("UniqueID", "")).lower():
                packs.append(dirpath)
    return sorted(packs)


def installed_mod_ids(mods_dir):
    ids = {"Pathoschild.ContentPatcher"}
    for dirpath, dirs, files in os.walk(mods_dir):
        if "manifest.json" in files:
            try:
                ids.add(str(sjson.load(os.path.join(dirpath, "manifest.json")).get("UniqueID")))
            except ValueError:
                pass
    return ids


def custom_locations(pack, c):
    n = 0
    for cl in pack.content.get("CustomLocations") or []:
        name = cl.get("Name")
        src = sjson.ci_path(pack.folder, pack.render(str(cl.get("FromMapFile", ""))))
        if name and src and c.load_file("Maps/" + name, src):
            n += 1
    return n


def apply_all(base, mods_dir, out, log=print, season="spring", maps_only=False):
    """season: which season's patches to apply. maps_only: skip data/image
    edits (used for the per-season map variants)."""
    BASELINE["season"] = season
    c = Content(base, out)
    log("  linking base content...")
    c.build()
    ids = installed_mod_ids(mods_dir)
    packs = [Pack(p, ids) for p in find_packs(mods_dir)]
    # Dependencies first; then bigger packs (base expansions) before smaller
    # ones that patch on top of them.
    def deps(p):
        return {str(d.get("UniqueID")) for d in p.manifest.get("Dependencies") or []}
    def size(p):
        return sum(len(f) for _, _, f in os.walk(p.folder))
    ordered, left = [], sorted(packs, key=lambda p: -size(p))
    while left:
        for p in left:
            if not (deps(p) & {q.id for q in left if q is not p}):
                ordered.append(p)
                left.remove(p)
                break
        else:
            ordered += left
            break
    packs = ordered
    log(f"  {len(packs)} Content Patcher packs: " + ", ".join(p.id for p in packs))
    plans = []
    for p in packs:
        p.resolve_dynamic()
        changes = []
        collect_changes(p, p.content.get("Changes"), p.folder, changes)
        plans.append((p, changes))
    stats = {k: 0 for k in ("load", "missing", "skipped_xnb", "entries", "fields", "textops", "images",
                            "maps", "map_areas", "map_tiles", "missing_target", "bad_field")}
    for p, _ in plans:
        stats["load"] += custom_locations(p, c)
    for p, changes in plans:                      # all loads first, like CP
        for ch in changes:
            if str(ch.get("Action", "")).lower() == "load":
                if maps_only and str(ch.get("FromFile", "")).lower().strip().endswith((".json", ".xnb")):
                    continue      # maps and their tilesheets still load
                apply_load(p, c, ch, stats)
    for p, changes in plans:                      # then edits, in pack order
        for ch in changes:
            a = str(ch.get("Action", "")).lower()
            if maps_only and a != "editmap" and not (a == "editdata" and "locations" in str(ch.get("Target", "")).lower()):
                continue
            try:
                if a == "editdata":
                    apply_editdata(p, c, ch, stats)
                elif a == "editimage":
                    apply_editimage(p, c, ch, stats)
                elif a == "editmap":
                    apply_editmap(p, c, ch, stats)
            except Exception as e:
                p.warnings.append(f"{a} {ch.get('Target')}: {e}")
    log("  writing merged files...")
    c.flush()
    log("  " + ", ".join(f"{k} {v}" for k, v in stats.items() if v))
    for p in packs:
        if p.warnings:
            log(f"  {p.id}: {len(p.warnings)} warnings (first: {p.warnings[0][:100]})")
    return c


if __name__ == "__main__":
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)
    apply_all(sys.argv[1], sys.argv[2], sys.argv[3])
