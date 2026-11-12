"""Runs tools/convert.py inside the web page (Pyodide, in a worker).

The folders the player picked are mounted read-only at /game, /mods and /save:
the page reads them where they are, nothing is uploaded and nothing is copied
that doesn't have to be. Converted files are written to /out (in memory) and
the page turns them into a zip or writes them to the SD card.
"""
import os
import shutil
import sys
import traceback

OUT = "/out"
MOUNTS = ("/game/", "/mods/", "/save/")
BIG = 1 << 20   # files this size or larger are never copied into memory

sys.path.insert(0, "/tools")


def _patch():
    """Memory is the scarce thing in a browser tab."""
    # No hard links here. A symlink does the same job for the merged Content
    # folder (cp.py removes the link before it rewrites a file).
    os.link = lambda src, dst, **kw: os.symlink(os.path.abspath(src), dst)

    plain_copy = shutil.copyfile

    def copyfile(src, dst, **kw):
        # Big files that go to the SD card unchanged (the sound banks, mod
        # music) stay where they are: the page reads them straight from the
        # picked folder when it writes the zip.
        try:
            real = os.path.realpath(src)
            if real.startswith(MOUNTS) and os.path.getsize(real) >= BIG:
                if os.path.lexists(dst):
                    os.remove(dst)
                os.symlink(real, dst)
                return dst
        except OSError:
            pass
        return plain_copy(src, dst, **kw)

    shutil.copyfile = copyfile

    import s3dweb       # the page's side: the XNB decompressor built for the browser
    import xnbfile
    from pyodide.ffi import to_js

    def decompress(body, mode, out_size):
        got = s3dweb.decompress(to_js(body), mode, out_size)
        data = got.to_bytes()
        return data if len(data) == out_size else None

    xnbfile.DECOMPRESS = decompress


def _subdirs(d):
    try:
        return sorted(e for e in os.listdir(d) if os.path.isdir(os.path.join(d, e)))
    except OSError:
        return []


def find_content(root="/game"):
    """The Content folder inside whatever was picked: the game folder, the
    Content folder itself, an unpacked copy, or the Mac app."""
    found = []
    level = [root]
    for _ in range(5):
        nxt = []
        for d in level:
            subs = _subdirs(d)
            if any(s.lower() == "maps" for s in subs) and any(s.lower() == "data" for s in subs):
                found.append(d)
            else:
                nxt += [os.path.join(d, s) for s in subs if s.lower() != "mods"]
        level = nxt
    if not found:
        return None
    # an unpacked copy converts faster than the packed one
    found.sort(key=lambda d: (0 if "unpacked" in d.lower() else 1, len(d)))
    return found[0]


def find_mods(content):
    """The Mods folder that was picked, or the one inside the game folder."""
    if os.path.isdir("/mods") and os.listdir("/mods"):
        return "/mods"
    d = os.path.dirname(content.rstrip("/"))
    for _ in range(3):
        for s in _subdirs(d):
            if s.lower() == "mods" and os.listdir(os.path.join(d, s)):
                return os.path.join(d, s)
        if d in ("/game", "/"):
            break
        d = os.path.dirname(d)
    return None


def find_save(root="/save"):
    """Saves/<Name_id>/<Name_id> (the file named like its folder)."""
    best = None
    for dirpath, _, files in os.walk(root):
        for f in files:
            if f.startswith("SaveGameInfo") or f.endswith(("_old", ".zip", ".txt", ".png")):
                continue
            p = os.path.join(dirpath, f)
            if f == os.path.basename(dirpath):
                return p
            if best is None or os.path.getsize(p) > os.path.getsize(best):
                best = p
    return best


def _clean():
    """Working copies aren't needed once the files for the SD card exist."""
    try:
        for e in os.listdir(OUT):
            if e.startswith("_"):
                shutil.rmtree(os.path.join(OUT, e), ignore_errors=True)
    except OSError:
        pass


def run():
    """Converts /game (+ mods, + save). Returns 0 when the files are ready."""
    _patch()
    content = find_content()
    if not content:
        print("! Couldn't find a Maps folder in what you picked. Pick your Stardew Valley folder "
              "(the one with Content inside), or the Content folder itself.")
        return 2
    mods = find_mods(content)
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT)
    print("Game files: " + content.replace("/game/", "", 1))
    print("Mods: " + ("none" if not mods else "the Mods folder you picked" if mods == "/mods"
                      else mods.replace("/game/", "", 1)))
    import warnings
    warnings.simplefilter("ignore", ResourceWarning)
    old = sys.argv
    sys.argv = ["convert.py", content, OUT, "--jobs=1"] + (["--mods=" + mods] if mods else [])
    try:
        import convert
        convert.main()
        code = 0
    except SystemExit as e:
        code = e.code if isinstance(e.code, int) else 1
        if e.code and not isinstance(e.code, int):
            print("Error: " + str(e.code))
    except MemoryError:
        print("! This browser tab ran out of memory. Close other tabs and try again, "
              "or use a desktop browser if you're on a phone.")
        code = 3
    except Exception:
        traceback.print_exc()
        code = 1
    finally:
        sys.argv = old
        _clean()
    if code == 0 and os.path.isdir("/save"):
        save = find_save()
        if save:
            print("Bringing your farm over: " + os.path.basename(save))
            try:
                import saveimport
                saveimport.import_save(save, os.path.join(OUT, "3ds", "stardew3ds"))
            except SystemExit as e:
                print("! Couldn't read that save: " + str(e.code))
            except Exception:
                traceback.print_exc()
                print("! Couldn't read that save. The game files are still fine.")
        else:
            print("! No save file in the save folder you picked")
    return code


def run_script(src, top):
    """For the page's own tests: runs a test script against the picked folder."""
    _patch()
    old = sys.argv
    sys.argv = ["test", "/game/" + top]
    try:
        exec(compile(src, "test", "exec"), {"__name__": "__main__", "__file__": "/tools/test.py"})
        return 0
    except SystemExit as e:
        return e.code if isinstance(e.code, int) else (1 if e.code else 0)
    finally:
        sys.argv = old


def outputs():
    """[path on the SD card, where it is now] for everything under /out/3ds."""
    res = []
    base = os.path.join(OUT, "3ds")
    for dirpath, _, files in os.walk(base):
        for f in sorted(files):
            p = os.path.join(dirpath, f)
            res.append(["3ds/" + os.path.relpath(p, base).replace(os.sep, "/"), os.path.realpath(p)])
    res.sort()
    return res
