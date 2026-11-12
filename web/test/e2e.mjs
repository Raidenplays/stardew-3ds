// Runs the web converter in a real (headless) Chrome and checks that what it makes is
// the same, file for file, as what tools/convert.py makes on a PC.
//   node web/test/e2e.mjs <built site> <fixtures dir> <expected "3ds" dir> [--game=sub] [--mods=sub] [--save=sub]
// The fixtures are made-up test files (tests/make_fake_content.py), never real game files.
import http from "node:http";
import fs from "node:fs";
import path from "node:path";
import os from "node:os";
import { execFileSync } from "node:child_process";
import puppeteer from "puppeteer-core";

const [site, fixtures, expected, ...rest] = process.argv.slice(2);
const opt = Object.fromEntries(rest.map((a) => a.replace(/^--/, "").split("=")));
const TYPES = { ".html": "text/html", ".js": "text/javascript", ".mjs": "text/javascript", ".css": "text/css",
  ".wasm": "application/wasm", ".json": "application/json", ".png": "image/png", ".zip": "application/zip" };

function walk(dir, base = dir, out = []) {
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) walk(p, base, out);
    else out.push(path.relative(base, p).split(path.sep).join("/"));
  }
  return out;
}

const list = (sub) => (sub ? walk(path.join(fixtures, sub)).map((p) => sub.split("/").pop() + "/" + p) : []);
const roots = {};
for (const k of ["game", "mods", "save"]) if (opt[k]) roots[opt[k].split("/").pop()] = path.join(fixtures, opt[k]);
const manifest = { base: "/fx/", game: list(opt.game), mods: list(opt.mods), save: list(opt.save) };
if (opt.script) manifest.script = "/fx/_script";

const server = http.createServer((req, res) => {
  const url = decodeURIComponent(req.url.split("?")[0]);
  let file;
  if (url === "/fx/manifest.json") { res.writeHead(200, { "content-type": "application/json" }); res.end(JSON.stringify(manifest)); return; }
  if (url === "/fx/_script") { res.writeHead(200, { "content-type": "text/plain" }); res.end(fs.readFileSync(opt.script)); return; }
  if (url.startsWith("/fx/")) {
    const [top, ...more] = url.slice(4).split("/");
    file = roots[top] && path.join(roots[top], ...more);
  } else file = path.join(site, url === "/" ? "index.html" : url);
  if (!file || !fs.existsSync(file) || fs.statSync(file).isDirectory()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { "content-type": TYPES[path.extname(file)] || "application/octet-stream" });
  fs.createReadStream(file).pipe(res);
});
await new Promise((r) => server.listen(0, "127.0.0.1", r));
const port = server.address().port;

const chrome = process.env.CHROME || ["/usr/bin/google-chrome", "/usr/bin/chromium-browser", "/usr/bin/chromium"].find((p) => fs.existsSync(p));
const down = fs.mkdtempSync(path.join(os.tmpdir(), "s3dweb-"));
const browser = await puppeteer.launch({ executablePath: chrome, headless: true, args: ["--no-sandbox", "--disable-dev-shm-usage"] });
let failed = false;
try {
  const page = await browser.newPage();
  const cdp = await page.createCDPSession();
  await cdp.send("Browser.setDownloadBehavior", { behavior: "allow", downloadPath: down });
  page.on("pageerror", (e) => console.log("page error:", e.message));
  page.on("console", (m) => { if (m.type() === "error") console.log("console:", m.text()); });
  await page.goto(`http://127.0.0.1:${port}/?test=/fx/manifest.json`);
  const t0 = Date.now();
  let phase = "";
  while (Date.now() - t0 < 20 * 60 * 1000) {
    phase = await page.evaluate(() => window.__s3d.phase);
    if (["zipped", "error", "failed"].includes(phase) || (opt.script && phase === "done")) break;
    await new Promise((r) => setTimeout(r, 500));
  }
  const log = await page.evaluate(() => window.__s3d.log.join("\n"));
  console.log(log.split("\n").slice(-25).join("\n"));
  console.log(`-- ${phase} after ${((Date.now() - t0) / 1000).toFixed(0)}s`);
  if (opt.script) {
    if (phase !== "done") throw new Error("the script failed in the page: " + phase);
    console.log("script passed in the page");
    await browser.close();
    server.close();
    process.exit(0);
  }
  if (phase !== "zipped") throw new Error("the page didn't finish: " + phase);
  const zip = path.join(down, "stardew3ds-sdcard.zip");
  for (let i = 0; i < 100 && !fs.existsSync(zip); i++) await new Promise((r) => setTimeout(r, 200));
  const got = path.join(down, "got");
  execFileSync("python3", ["-c", "import sys,zipfile; z=zipfile.ZipFile(sys.argv[1]); assert z.testzip() is None; z.extractall(sys.argv[2])", zip, got]);
  const want = walk(expected).filter((p) => !p.startsWith("stardew3ds.3dsx"));
  const have = new Set(walk(path.join(got, "3ds")));
  let bad = 0;
  for (const p of want) {
    if (!have.has(p)) { console.log("missing:", p); bad++; continue; }
    if (!fs.readFileSync(path.join(expected, p)).equals(fs.readFileSync(path.join(got, "3ds", p)))) { console.log("differs:", p); bad++; }
    have.delete(p);
  }
  have.delete("stardew3ds.3dsx");
  for (const p of have) { console.log("extra:", p); bad++; }
  console.log(`${want.length} files compared, ${bad} wrong`);
  if (bad) throw new Error("the web converter's files differ from the PC converter's");
  console.log("web converter: same output as tools/convert.py");
} catch (e) {
  console.log("FAILED:", e.message);
  failed = true;
} finally {
  await browser.close();
  server.close();
}
process.exit(failed ? 1 : 0);
