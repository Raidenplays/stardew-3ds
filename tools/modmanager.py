#!/usr/bin/env python3
"""
stardew3ds mod manager (desktop side of the 3DS mod loader).

Keeps a library of mod zips/folders, checks them, converts each one into a
layer for the 3DS, and copies the result to your SD card. On the 3DS, new
layers show up on the boot mod screen (turned on), and your save is backed
up automatically before a different mod set loads.

    modmanager.py                         open the window (tkinter)
    modmanager.py add <zip or folder>...  add mods to the library
    modmanager.py remove <id>
    modmanager.py list                    show mods, what kind they are, problems
    modmanager.py build                   convert base game + every mod layer
    modmanager.py deploy <SD root>        copy to <SD root>/3ds/stardew3ds/

Settings live in ~/.stardew3ds/settings.json: "content" (your
'Content (unpacked)' folder) and "library" (default ~/.stardew3ds/mods).
Set them with:  modmanager.py set content "<path>"
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sjson  # noqa: E402
import layer  # noqa: E402

HOME = os.path.join(os.path.expanduser("~"), ".stardew3ds")
SETTINGS = os.path.join(HOME, "settings.json")

# Frameworks the converter understands (content packs for these become data).
CONVERTS = {
    "pathoschild.contentpatcher": "Content Patcher pack",
    "esca.farmtypemanager": "Farm Type Manager pack",
}
# Code frameworks: their packs convert, the DLL itself is never needed.
# Listed so mods that require them don't show as broken.
EMULATED = {
    "pathoschild.contentpatcher": "built in",
    "esca.farmtypemanager": "built in (spawns)",
    "spacecore": "partial (content only)",
    "spacechase0.spacecore": "partial (content only)",
    "spacechase0.jsonassets": "not yet",
    "peacefulend.customcompanions": "not yet",
    "smapi": "built in",
}


# ---------------------------------------------------------------- settings
def settings():
    s = {"content": "", "library": os.path.join(HOME, "mods"), "build": os.path.join(HOME, "build")}
    if os.path.isfile(SETTINGS):
        with open(SETTINGS) as f:
            s.update(json.load(f))
    return s


def save_settings(s):
    os.makedirs(HOME, exist_ok=True)
    with open(SETTINGS, "w") as f:
        json.dump(s, f, indent=2)


# ---------------------------------------------------------------- library
def _manifests(folder):
    """[(dir, manifest)] for every manifest.json under folder."""
    out = []
    for dirpath, dirs, files in os.walk(folder):
        dirs[:] = [d for d in dirs if not d.startswith(".")]
        if "manifest.json" in files:
            try:
                out.append((dirpath, sjson.load(os.path.join(dirpath, "manifest.json"))))
            except Exception as e:  # noqa: BLE001
                out.append((dirpath, {"_error": str(e)}))
    return out


def _slug(s):
    s = "".join(ch.lower() if ch.isalnum() else "-" for ch in s)
    while "--" in s:
        s = s.replace("--", "-")
    return s.strip("-")[:40] or "mod"


def add(path, lib):
    """Adds a zip or folder as one library entry (all its parts together)."""
    os.makedirs(lib, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="s3ds-add-")
    try:
        if zipfile.is_zipfile(path):
            with zipfile.ZipFile(path) as z:
                for m in z.infolist():   # refuse paths escaping the folder
                    target = os.path.realpath(os.path.join(tmp, m.filename))
                    if not target.startswith(os.path.realpath(tmp) + os.sep) and target != os.path.realpath(tmp):
                        raise ValueError(f"unsafe path in zip: {m.filename}")
                z.extractall(tmp)
            src = tmp
        else:
            src = path
        mans = _manifests(src)
        if not mans:
            raise ValueError("no manifest.json inside - is this a SMAPI mod?")
        # name the entry after the "main" part: the one others depend on least
        main = max(mans, key=lambda m: (not m[1].get("EntryDll"), -len(m[0])))[1]
        entry_id = _slug(main.get("UniqueID") or os.path.basename(path))
        dest = os.path.join(lib, entry_id)
        if os.path.exists(dest):
            shutil.rmtree(dest)
        # keep only the mod folders (each manifest dir), under the entry
        os.makedirs(dest)
        for d, man in mans:
            name = os.path.basename(d.rstrip(os.sep)) or entry_id
            shutil.copytree(d, os.path.join(dest, name), dirs_exist_ok=True)
        return entry_id
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def scan(lib):
    """Library entries with their parts, kinds and problems."""
    entries = []
    if not os.path.isdir(lib):
        return entries
    for e in sorted(os.listdir(lib)):
        folder = os.path.join(lib, e)
        if not os.path.isdir(folder):
            continue
        parts = []
        for d, man in _manifests(folder):
            if "_error" in man:
                parts.append({"dir": d, "id": "?", "name": os.path.basename(d), "kind": "broken",
                              "deps": [], "error": man["_error"]})
                continue
            pack_for = str((man.get("ContentPackFor") or {}).get("UniqueID", "")).lower()
            if pack_for in CONVERTS:
                kind = CONVERTS[pack_for]
            elif man.get("EntryDll"):
                kind = "code"
            elif pack_for:
                kind = "pack for " + pack_for
            else:
                kind = "unknown"
            deps = [(str(d.get("UniqueID")), d.get("IsRequired", True) is not False)
                    for d in man.get("Dependencies") or []]
            if pack_for:
                deps.append((pack_for, True))
            parts.append({"dir": d, "id": str(man.get("UniqueID")), "name": man.get("Name", ""),
                          "version": str(man.get("Version", "")), "author": man.get("Author", ""),
                          "description": man.get("Description", ""), "kind": kind, "deps": deps})
        entries.append({"id": e, "folder": folder, "parts": parts})
    # problems need the whole library
    have = {p["id"].lower(): ent["id"] for ent in entries for p in ent["parts"]}
    for ent in entries:
        probs, needs = [], set()
        convertible = [p for p in ent["parts"] if p["kind"] in CONVERTS.values()]
        if not convertible:
            if any(p["kind"] == "code" for p in ent["parts"]):
                probs.append("code-only mod: nothing to convert (C# can't run on the 3DS)")
            else:
                probs.append("nothing the converter understands")
        for p in ent["parts"]:
            if p["kind"].startswith("pack for "):
                fw = p["kind"][9:]
                probs.append(f"{p['name']}: needs {fw}, which the 3DS can't use yet")
            if p["kind"] == "broken":
                probs.append(f"{p['name']}: manifest unreadable ({p['error']})")
            for dep, required in p["deps"]:
                dl = dep.lower()
                if dl in have and have[dl] != ent["id"]:
                    needs.add(have[dl])
                elif dl in have:
                    pass
                elif dl in EMULATED:
                    if EMULATED[dl] in ("not yet",) and required:
                        probs.append(f"uses {dep} (its extra features won't work yet)")
                elif required:
                    probs.append(f"missing required mod {dep}")
        ent["needs"] = sorted(needs)
        ent["problems"] = probs
        main = next((p for p in ent["parts"] if p["kind"] == CONVERTS["pathoschild.contentpatcher"]), None) \
            or (ent["parts"][0] if ent["parts"] else {})
        ent["name"] = main.get("name") or ent["id"]
        ent["version"] = main.get("version", "")
        ent["author"] = main.get("author", "")
        ent["description"] = main.get("description", "")
    return entries


def _closure(ent_id, by_id, seen=None):
    seen = seen if seen is not None else []
    for n in by_id[ent_id]["needs"]:
        if n not in seen and n in by_id:
            _closure(n, by_id, seen)
            seen.append(n)
    return seen


# ---------------------------------------------------------------- build
def _hash_dirs(paths):
    h = hashlib.sha1()
    for p in paths:
        for dirpath, dirs, files in os.walk(p):
            dirs.sort()
            for fn in sorted(files):
                full = os.path.join(dirpath, fn)
                st = os.stat(full)
                h.update(f"{os.path.relpath(full, p)}|{st.st_size}|{int(st.st_mtime)}".encode())
    return h.hexdigest()


def _convert(content, out, mods_dir=None, log=print):
    cmd = [sys.executable, "-I", os.path.join(HERE, "convert.py"), content, out]
    if mods_dir:
        cmd.append("--mods=" + mods_dir)
    log("  $ convert" + (" (with mods)" if mods_dir else " (base game)"))
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(r.stdout[-2000:] + r.stderr[-2000:])
    return os.path.join(out, "3ds", "stardew3ds")


def _stage(entry_ids, by_id, where):
    """Puts the given library entries side by side in a fresh mods folder."""
    if os.path.exists(where):
        shutil.rmtree(where)
    os.makedirs(where)
    for eid in entry_ids:
        shutil.copytree(by_id[eid]["folder"], os.path.join(where, eid))
    return where


def build(s, log=print, only=None):
    content, lib, bdir = s["content"], s["library"], s["build"]
    if not content or not os.path.isdir(content):
        raise RuntimeError("set your 'Content (unpacked)' folder first: modmanager.py set content <path>")
    os.makedirs(bdir, exist_ok=True)
    stamp_path = os.path.join(bdir, "stamps.json")
    stamps = json.load(open(stamp_path)) if os.path.isfile(stamp_path) else {}
    entries = scan(lib)
    by_id = {e["id"]: e for e in entries}

    # base game, cached
    base_out = os.path.join(bdir, "base")
    base_root = os.path.join(base_out, "3ds", "stardew3ds")
    key = _hash_dirs([content]) + "|" + _hash_dirs([HERE])
    if stamps.get("base") != key or not os.path.isdir(base_root):
        log("Converting the base game...")
        _convert(content, base_out, log=log)
        stamps = {"base": key}
    layers = os.path.join(bdir, "layers")
    os.makedirs(layers, exist_ok=True)

    built = []
    for ent in entries:
        if only and ent["id"] not in only:
            continue
        if not any(p["kind"] in CONVERTS.values() for p in ent["parts"]):
            log(f"skip {ent['name']}: {'; '.join(ent['problems'])}")
            continue
        deps = _closure(ent["id"], by_id)
        k = key + "|" + _hash_dirs([by_id[d]["folder"] for d in deps + [ent["id"]]])
        out_dir = os.path.join(layers, ent["id"])
        if stamps.get(ent["id"]) == k and os.path.isdir(out_dir):
            log(f"{ent['name']}: up to date")
            built.append(ent["id"])
            continue
        log(f"{ent['name']}" + (f" (on top of {', '.join(deps)})" if deps else "") + "...")
        with tempfile.TemporaryDirectory(prefix="s3ds-build-") as tmp:
            # layer = (deps + this mod) minus (deps alone), so each layer only
            # holds its own changes even when mods build on each other
            under = base_root
            if deps:
                under = _convert(content, os.path.join(tmp, "under"),
                                 _stage(deps, by_id, os.path.join(tmp, "m_under")), log)
            top = _convert(content, os.path.join(tmp, "top"),
                           _stage(deps + [ent["id"]], by_id, os.path.join(tmp, "m_top")), log)
            desc = ent["description"]
            if deps:
                desc = (desc + " " if desc else "") + "Needs: " + ", ".join(by_id[d]["name"] for d in deps) + "."
            st = layer.build_layer(under, top, out_dir, {
                "name": ent["name"], "version": ent["version"], "author": ent["author"],
                "description": desc})
            if deps:
                with open(os.path.join(out_dir, "mod.txt"), "a") as f:
                    f.write("needs: " + ",".join(deps) + "\n")
        log("   " + (", ".join(f"{k2} {v}" for k2, v in st.items()) or "no changes"))
        stamps[ent["id"]] = k
        built.append(ent["id"])
        with open(stamp_path, "w") as f:
            json.dump(stamps, f)
    with open(stamp_path, "w") as f:
        json.dump(stamps, f)
    return built


def deploy(s, sd_root, log=print):
    bdir = s["build"]
    base_root = os.path.join(bdir, "base", "3ds", "stardew3ds")
    if not os.path.isdir(base_root):
        raise RuntimeError("nothing built yet - run build first")
    dest = os.path.join(sd_root, "3ds", "stardew3ds")
    os.makedirs(dest, exist_ok=True)
    keep = {"save.sav", "save.sav.mods", "mods.txt", "backups", "mods"}
    log(f"Copying base game to {dest} ...")
    for name in os.listdir(base_root):
        if name in keep:
            continue
        src, dst = os.path.join(base_root, name), os.path.join(dest, name)
        if os.path.isdir(src):
            shutil.copytree(src, dst, dirs_exist_ok=True)
        else:
            shutil.copyfile(src, dst)
    layers = os.path.join(bdir, "layers")
    lib_ids = {e["id"] for e in scan(s["library"])}
    os.makedirs(os.path.join(dest, "mods"), exist_ok=True)
    for lid in sorted(os.listdir(layers)) if os.path.isdir(layers) else []:
        if lid not in lib_ids:
            continue
        d = os.path.join(dest, "mods", lid)
        if os.path.exists(d):
            shutil.rmtree(d)
        shutil.copytree(os.path.join(layers, lid), d)
        log(f"  mod {lid}")
    log("Done. Saves, backups and your on/off choices on the 3DS were left alone.")


# ---------------------------------------------------------------- CLI
def cmd_list(s):
    ents = scan(s["library"])
    if not ents:
        print("Library is empty. Add mods with: modmanager.py add <zip>")
    for e in ents:
        print(f"{e['name']} {e['version']}  [{e['id']}]")
        for p in e["parts"]:
            print(f"    - {p['name']}: {p['kind']}")
        if e["needs"]:
            print(f"    needs: {', '.join(e['needs'])}")
        for pr in e["problems"]:
            print(f"    ! {pr}")


def gui():
    import threading
    import tkinter as tk
    from tkinter import filedialog, messagebox

    s = settings()
    root = tk.Tk()
    root.title("stardew3ds mod manager")
    root.geometry("760x520")
    top = tk.Frame(root)
    top.pack(fill="x", padx=8, pady=6)
    content_var = tk.StringVar(value=s["content"] or "(not set)")
    tk.Label(top, text="Game content:").pack(side="left")
    tk.Label(top, textvariable=content_var, fg="#555").pack(side="left", padx=4)

    def pick_content():
        d = filedialog.askdirectory(title="Your 'Content (unpacked)' folder")
        if d:
            s["content"] = d
            save_settings(s)
            content_var.set(d)

    tk.Button(top, text="Change...", command=pick_content).pack(side="left")

    mid = tk.PanedWindow(root, orient="horizontal")
    mid.pack(fill="both", expand=True, padx=8)
    lst = tk.Listbox(mid, width=34)
    info = tk.Text(mid, wrap="word", height=10)
    mid.add(lst)
    mid.add(info)
    logbox = tk.Text(root, height=8, bg="#1e1e1e", fg="#ddd")
    logbox.pack(fill="x", padx=8, pady=4)

    def log(msg):
        root.after(0, lambda: (logbox.insert("end", msg + "\n"), logbox.see("end")))

    ents = []

    def refresh():
        nonlocal ents
        ents = scan(s["library"])
        lst.delete(0, "end")
        for e in ents:
            mark = "!" if e["problems"] else " "
            lst.insert("end", f"{mark} {e['name']} {e['version']}")

    def show(_=None):
        sel = lst.curselection()
        info.delete("1.0", "end")
        if not sel:
            return
        e = ents[sel[0]]
        lines = [f"{e['name']} {e['version']}  by {e['author']}", "", e["description"], ""]
        lines += [f"• {p['name']} — {p['kind']}" for p in e["parts"]]
        if e["needs"]:
            lines += ["", "Builds on: " + ", ".join(e["needs"])]
        if e["problems"]:
            lines += ["", "Problems:"] + ["  " + p for p in e["problems"]]
        info.insert("1.0", "\n".join(lines))

    lst.bind("<<ListboxSelect>>", show)

    def do_add():
        for p in filedialog.askopenfilenames(title="Mod zips", filetypes=[("Zip", "*.zip"), ("All", "*")]):
            try:
                log("added " + add(p, s["library"]))
            except Exception as ex:  # noqa: BLE001
                messagebox.showerror("Couldn't add", f"{os.path.basename(p)}: {ex}")
        refresh()

    def do_remove():
        sel = lst.curselection()
        if sel and messagebox.askyesno("Remove", f"Remove {ents[sel[0]]['name']} from the library?"):
            shutil.rmtree(ents[sel[0]]["folder"])
            refresh()

    def background(fn):
        def run():
            try:
                fn()
            except Exception as ex:  # noqa: BLE001
                log("ERROR: " + str(ex))
        threading.Thread(target=run, daemon=True).start()

    def do_build():
        background(lambda: (build(s, log), log("Build finished.")))

    def do_deploy():
        d = filedialog.askdirectory(title="SD card root")
        if d:
            background(lambda: deploy(s, d, log))

    bar = tk.Frame(root)
    bar.pack(fill="x", padx=8, pady=6)
    for label, fn in (("Add mods...", do_add), ("Remove", do_remove), ("Build", do_build),
                      ("Copy to SD card...", do_deploy)):
        tk.Button(bar, text=label, command=fn).pack(side="left", padx=3)
    refresh()
    root.mainloop()


def main():
    s = settings()
    a = sys.argv[1:]
    if not a:
        return gui()
    if a[0] == "set" and len(a) == 3:
        s[a[1]] = os.path.abspath(a[2])
        save_settings(s)
    elif a[0] == "add":
        for p in a[1:]:
            print("added", add(p, s["library"]))
    elif a[0] == "remove":
        shutil.rmtree(os.path.join(s["library"], a[1]))
    elif a[0] == "list":
        cmd_list(s)
    elif a[0] == "build":
        build(s, only=set(a[1:]) or None)
    elif a[0] == "deploy" and len(a) == 2:
        deploy(s, a[1])
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
