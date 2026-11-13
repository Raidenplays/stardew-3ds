// The converter itself: the same Python tools as the Android app and tools/convert.py,
// run inside this page with Pyodide. Everything happens in this tab; the only requests
// it makes are for the page's own files.
import { loadPyodide } from "./pyodide/pyodide.mjs";
import S3dXnb from "./s3dxnb.mjs";
import { StoreZip } from "./zip.js";

let pyodide = null;
let xnb = null;
let ready = null;
const sources = new Map();   // "/game/..." -> the File the player picked (for files passed straight through)
let outputs = [];            // [path on the SD card, path in this worker's files]
let game3dsx = null;         // the newest stardew3ds.3dsx, served with the page

const post = (m) => self.postMessage(m);
const log = (line) => post({ t: "log", line });
const progress = (pct, what) => post({ t: "progress", pct, what });

// converter log lines -> how far along we are
const STAGES = [
  ["Unpacking your game files", 12], ["unpacked", 30], ["Mods...", 32], ["mods merged", 44],
  ["Maps...", 46], ["maps converted", 62], ["Seasonal maps", 64], ["Data...", 74], ["Villagers...", 80],
  ["Events...", 84], ["Sound...", 86], ["Fonts...", 88], ["Textures", 90], ["Done in", 97],
];
function stage(line) {
  for (const [key, pct] of STAGES) if (line.includes(key)) { progress(pct, line.trim().slice(0, 70)); return; }
}

async function init() {
  progress(1, "Starting the converter...");
  pyodide = await loadPyodide({ indexURL: new URL("./pyodide/", import.meta.url).href });
  progress(4, "Loading the image tools...");
  await pyodide.loadPackage(["numpy", "pillow"]);
  xnb = await S3dXnb();
  // The game's packed .xnb files are opened by the same C++ as on the 3DS, built for the browser.
  pyodide.registerJsModule("s3dweb", {
    decompress(body, mode, outSize) {
      const inPtr = xnb._malloc(body.length || 1);
      const outPtr = xnb._malloc(outSize || 1);
      try {
        xnb.HEAPU8.set(body, inPtr);
        const ok = xnb._s3d_xnb_decompress(inPtr, body.length, mode, outPtr, outSize);
        return ok ? xnb.HEAPU8.slice(outPtr, outPtr + outSize) : new Uint8Array(0);
      } finally {
        xnb._free(inPtr);
        xnb._free(outPtr);
      }
    },
  });
  const files = await (await fetch("tools/files.json")).json();
  pyodide.FS.mkdirTree("/tools");
  for (const f of files) {
    const r = await fetch("tools/" + f);
    if (!r.ok) throw new Error("couldn't load " + f);
    pyodide.FS.writeFile("/tools/" + f, await r.text());
  }
  const out = (line) => { log(line); stage(line); };
  pyodide.setStdout({ batched: out });
  pyodide.setStderr({ batched: out });
  try {
    const r = await fetch("stardew3ds.3dsx");
    if (r.ok) game3dsx = new Uint8Array(await r.arrayBuffer());
  } catch (e) { /* the zip just won't carry the game */ }
  progress(8, "Ready");
  post({ t: "ready" });
}

function unmount(dir) {
  try { pyodide.FS.unmount(dir); } catch (e) { /* wasn't mounted */ }
  try { pyodide.FS.rmdir(dir); } catch (e) { /* not there */ }
}

// A picked folder appears to Python as an ordinary read-only folder. Its files are read
// where they are, only when the converter opens them.
function mount(dir, entries) {
  unmount(dir);
  for (const k of [...sources.keys()]) if (k.startsWith(dir + "/")) sources.delete(k);
  if (!entries || !entries.length) return;
  const FS = pyodide.FS;
  FS.mkdirTree(dir);
  for (const e of entries) sources.set(dir + "/" + e.path, e.file);
  if (FS.filesystems.WORKERFS) {
    FS.mount(FS.filesystems.WORKERFS, { blobs: entries.map((e) => ({ name: e.path, data: e.file })) }, dir);
  } else {
    // no lazy file system in this build: copy what was picked into memory
    log("(copying the picked files into memory)");
    const reader = new FileReaderSync();
    for (const e of entries) {
      const p = dir + "/" + e.path;
      FS.mkdirTree(p.slice(0, p.lastIndexOf("/")));
      FS.writeFile(p, new Uint8Array(reader.readAsArrayBuffer(e.file)));
    }
  }
}

async function convert(msg) {
  await ready;
  progress(9, "Reading your folders...");
  mount("/game", msg.game);
  mount("/mods", msg.mods);
  mount("/save", msg.save);
  outputs = [];
  progress(10, "Converting...");
  pyodide.runPython("import sys\nif '/tools' not in sys.path: sys.path.insert(0, '/tools')");
  const code = await pyodide.runPythonAsync("import bridge\nbridge.run()");
  if (code !== 0) {
    progress(0, "Stopped (see the log)");
    post({ t: "done", ok: false });
    return;
  }
  const list = pyodide.runPython("import bridge\nbridge.outputs()");
  outputs = list.toJs();
  list.destroy();
  let bytes = 0;
  for (const [, where] of outputs) bytes += pyodide.FS.stat(where).size;
  if (game3dsx) bytes += game3dsx.length;
  progress(100, "Done");
  post({ t: "done", ok: true, files: outputs.length + (game3dsx ? 1 : 0), bytes });
}

// One converted file, as something that can be written out: bytes, or the player's own
// File when the converter passed it through untouched.
function piece(where) {
  const src = sources.get(where);
  return src || pyodide.FS.readFile(where);
}

function* everything() {
  if (game3dsx) yield ["3ds/stardew3ds.3dsx", game3dsx];
  for (const [name, where] of outputs) yield [name, piece(where)];
}

function makeZip() {
  const zip = new StoreZip();
  const total = outputs.length + 1;
  let n = 0;
  for (const [name, data] of everything()) {
    zip.add(name, data);
    if (++n % 50 === 0) progress(Math.round((n * 100) / total), "Zipping: " + n + " of " + total + " files");
  }
  const blob = zip.finish();
  progress(100, "Zip ready");
  post({ t: "zip", blob });
}

// Chromium browsers can write straight into a folder the player picks (the SD card).
async function saveTo(root) {
  const dirs = new Map([["", root]]);
  async function dirFor(path) {
    if (dirs.has(path)) return dirs.get(path);
    const cut = path.lastIndexOf("/");
    const parent = await dirFor(cut < 0 ? "" : path.slice(0, cut));
    const d = await parent.getDirectoryHandle(path.slice(cut + 1), { create: true });
    dirs.set(path, d);
    return d;
  }
  const total = outputs.length + 1;
  let n = 0;
  for (const [name, data] of everything()) {
    const cut = name.lastIndexOf("/");
    const d = await dirFor(name.slice(0, cut));
    const fh = await d.getFileHandle(name.slice(cut + 1), { create: true });
    const w = await fh.createWritable();
    await w.write(data);
    await w.close();
    if (++n % 20 === 0) progress(Math.round((n * 100) / total), "Saving: " + n + " of " + total + " files");
  }
  progress(100, "Saved " + n + " files");
  post({ t: "saved", files: n });
}

self.onmessage = async (ev) => {
  const msg = ev.data;
  try {
    if (msg.cmd === "init") { ready = init(); await ready; }
    else if (msg.cmd === "convert") await convert(msg);
    else if (msg.cmd === "zip") makeZip();
    else if (msg.cmd === "script") {
      await ready;
      mount("/game", msg.game);
      pyodide.runPython("import sys\nif '/tools' not in sys.path: sys.path.insert(0, '/tools')");
      pyodide.globals.set("_src", msg.src);
      pyodide.globals.set("_top", msg.game[0].path.split("/")[0]);
      const code = pyodide.runPython("import bridge\nbridge.run_script(_src, _top)");
      post({ t: "done", ok: code === 0, files: 0, bytes: 0 });
    }
    else if (msg.cmd === "saveTo") await saveTo(msg.dir);
  } catch (e) {
    post({ t: "error", msg: String((e && e.message) || e), during: msg.cmd });
  }
};
