"""Runs tools/convert.py for the Android app, sending its output to a callback."""
import os
import sys
import traceback


class _Lines:
    def __init__(self, cb):
        self.cb = cb
        self.buf = ""

    def write(self, s):
        self.buf += s
        while "\n" in self.buf:
            line, self.buf = self.buf.split("\n", 1)
            self.cb.onLine(line)

    def flush(self):
        if self.buf:
            self.cb.onLine(self.buf)
            self.buf = ""


_CONVERTER_MODULES = ("convert", "cp", "sjson", "tbin", "tmxmap", "xnb", "layer")


def run(content, mods, out, logo, cb, tools_dir=""):
    # use the converter downloaded from the repo when there is one
    if tools_dir and os.path.isfile(os.path.join(tools_dir, "convert.py")):
        if tools_dir in sys.path:
            sys.path.remove(tools_dir)
        sys.path.insert(0, tools_dir)
        for m in _CONVERTER_MODULES:
            sys.modules.pop(m, None)
    import convert
    args = ["convert.py", content, out, "--jobs=1"]   # no extra processes on a phone
    if mods:
        args.append("--mods=" + mods)
    if logo:
        args.append("--logo=" + logo)
    old = sys.stdout, sys.stderr, sys.argv
    sys.stdout = sys.stderr = _Lines(cb)
    sys.argv = args
    try:
        convert.main()
        return 0
    except SystemExit as e:
        return e.code if isinstance(e.code, int) else 1
    except Exception:
        traceback.print_exc()
        return 1
    finally:
        try:
            sys.stdout.flush()
        except Exception:
            pass
        sys.stdout, sys.stderr, sys.argv = old
