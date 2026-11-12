#!/bin/sh
# Puts the web converter together in a folder that can be served as it is.
#   web/build.sh <out dir> [stardew3ds.3dsx] [build label]
# Needs: emcc (Emscripten), npm, curl, python3. Run from the repo root.
set -e
OUT="${1:?out dir}"
GAME="$2"
LABEL="$3"
PYODIDE="${PYODIDE_VERSION:-314.0.7}"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$HERE")"

rm -rf "$OUT"
mkdir -p "$OUT/tools" "$OUT/pyodide"
cp "$HERE/index.html" "$HERE/style.css" "$HERE/app.js" "$HERE/worker.js" "$HERE/zip.js" "$ROOT/icon.png" "$OUT/"

# the converter: the same files the Android app downloads, plus the page's glue
FILES="convert.py cp.py sjson.py tbin.py tmxmap.py xnb.py layer.py xnbfile.py xnbunpack.py netschema.py saveimport.py"
for f in $FILES; do cp "$ROOT/tools/$f" "$OUT/tools/"; done
cp "$HERE/bridge.py" "$OUT/tools/"
python3 -c "import json,sys; json.dump(sys.argv[1:], open('$OUT/tools/files.json','w'))" $FILES bridge.py

# the XNB decompressor (the 3DS setup's own C++), built for the browser
emcc -O2 -std=c++17 -I"$ROOT/source" "$ROOT/tools/native/s3dxnb.cpp" "$ROOT/source/setup/lzx.cpp" "$ROOT/source/setup/xnb.cpp" \
  -o "$OUT/s3dxnb.mjs" -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=S3dXnb -sEXPORTED_FUNCTIONS=_s3d_xnb_decompress,_malloc,_free \
  -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=worker -sFILESYSTEM=0

# Python for the browser, served from this same site (no third-party requests when the page runs)
TMP="$(mktemp -d)"
(cd "$TMP" && npm init -y >/dev/null && npm install --silent "pyodide@$PYODIDE" >/dev/null)
P="$TMP/node_modules/pyodide"
cp "$P/pyodide.mjs" "$P/pyodide.asm.mjs" "$P/pyodide.asm.wasm" "$P/python_stdlib.zip" "$P/pyodide-lock.json" "$OUT/pyodide/"
if [ -z "$SKIP_WHEELS" ]; then
  python3 - "$P/pyodide-lock.json" "$OUT/pyodide" "$PYODIDE" <<'EOF'
import hashlib, json, subprocess, sys
lock, out, ver = sys.argv[1:4]
pk = json.load(open(lock))["packages"]
todo, seen = ["numpy", "pillow"], set()
while todo:
    name = todo.pop()
    if name in seen:
        continue
    seen.add(name)
    e = pk[name]
    todo += e.get("depends", [])
    dst = f"{out}/{e['file_name']}"
    subprocess.run(["curl", "-sSfL", "--retry", "3", "-o", dst,
                    f"https://cdn.jsdelivr.net/pyodide/v{ver}/full/{e['file_name']}"], check=True)
    if hashlib.sha256(open(dst, "rb").read()).hexdigest() != e["sha256"]:
        sys.exit(f"{e['file_name']}: checksum doesn't match Pyodide's lock file")
    print("  " + e["file_name"])
EOF
fi
rm -rf "$TMP"

if [ -n "$GAME" ] && [ -f "$GAME" ]; then cp "$GAME" "$OUT/stardew3ds.3dsx"; fi
if [ -n "$LABEL" ]; then printf '%s\n' "$LABEL" > "$OUT/build.txt"; fi
touch "$OUT/.nojekyll"
echo "web converter ready in $OUT"
