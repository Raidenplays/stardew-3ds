// The page: pick folders, start the converter (worker.js), hand over the result.
(function () {
  const $ = (id) => document.getElementById(id);
  const picked = { game: [], mods: [], save: [] };
  const DLL = "StardewValley.GameData.dll";
  let dll = null;           // that file, picked on its own
  let needDll = false;      // a packed game folder without it
  let worker = null;
  let ready = false;
  let busy = false;
  let converted = false;
  let waitingKind = null;   // which folder the plain file input is being used for
  const state = { phase: "loading", log: [] };
  window.__s3d = state;     // the test in web/test reads this

  const label = { game: $("gameName"), mods: $("modsName"), save: $("saveName") };

  function say(line) {
    state.log.push(line);
    const log = $("log");
    log.textContent += line + "\n";
    log.scrollTop = log.scrollHeight;
  }
  function status(text) { $("status").textContent = text; }
  function bar(pct) { $("barFill").style.width = Math.max(0, Math.min(100, pct)) + "%"; }

  function refresh() {
    $("convert").disabled = busy || !ready || !picked.game.length || (needDll && !dll);
    $("dllBox").hidden = !needDll;
    $("pickDll").disabled = busy;
    for (const id of ["pickGame", "pickMods", "pickSave", "zip", "saveTo", "farm"]) $(id).disabled = busy;
    $("clearMods").hidden = !picked.mods.length;
    $("clearSave").hidden = !picked.save.length;
    $("result").hidden = !converted;
  }

  function setBusy(b) { busy = b; refresh(); }
  window.addEventListener("beforeunload", (e) => { if (busy) { e.preventDefault(); e.returnValue = ""; } });

  // ------------------------------------------------------------ the converter
  function startWorker() {
    worker = new Worker("worker.js", { type: "module" });
    worker.onmessage = (ev) => {
      const m = ev.data;
      if (m.t === "log") say(m.line);
      else if (m.t === "progress") { bar(m.pct); status(m.what); }
      else if (m.t === "ready") {
        ready = true;
        state.phase = "ready";
        bar(0);
        hint();
        refresh();
      } else if (m.t === "done") {
        converted = m.ok;
        state.phase = m.ok ? "done" : "failed";
        if (m.ok) {
          $("resultInfo").textContent = "Your 3DS files are ready: " + m.files + " files, " + mb(m.bytes) + ".";
          status("Done. Download the zip below" + ($("saveTo").hidden ? "." : ", or save straight to your SD card."));
          $("result").scrollIntoView({ behavior: "smooth", block: "nearest" });
        } else {
          $("logBox").open = true;
          status("The converter stopped. The details below say why.");
        }
        setBusy(false);
      } else if (m.t === "zip") {
        const a = document.createElement("a");
        a.href = URL.createObjectURL(m.blob);
        a.download = "stardew3ds-sdcard.zip";
        document.body.appendChild(a);
        a.click();
        a.remove();
        setTimeout(() => URL.revokeObjectURL(a.href), 60000);
        state.phase = "zipped";
        status("Zip downloaded (" + mb(m.blob.size) + "). Unzip it onto your SD card.");
        setBusy(false);
      } else if (m.t === "saved") {
        state.phase = "saved";
        status("Saved " + m.files + " files. Your SD card is ready.");
        setBusy(false);
      } else if (m.t === "error") {
        state.phase = "error";
        say("! " + m.msg);
        $("logBox").open = true;
        status(m.during === "init"
          ? "The converter couldn't start in this browser. Try a current Chrome, Edge or Firefox on a computer."
          : "Something went wrong. The details below say what.");
        setBusy(false);
      }
    };
    worker.onerror = (e) => {
      state.phase = "error";
      say("! " + (e.message || "the converter crashed"));
      $("logBox").open = true;
      status("The converter crashed. Reload the page and try again.");
      setBusy(false);
    };
    worker.postMessage({ cmd: "init" });
  }

  function mb(n) { return n >= 1 << 20 ? (n / (1 << 20)).toFixed(0) + " MB" : Math.ceil(n / 1024) + " KB"; }

  // ------------------------------------------------------------ picking folders
  async function walk(dir, path, out, note) {
    for await (const [name, h] of dir.entries()) {
      const p = path + "/" + name;
      if (h.kind === "directory") await walk(h, p, out, note);
      else {
        out.push({ path: p, file: await h.getFile() });
        if (out.length % 500 === 0) note(out.length);
      }
    }
  }

  async function pick(kind) {
    if (busy) return;
    if (window.showDirectoryPicker) {
      let dir;
      try { dir = await window.showDirectoryPicker({ id: "s3d-" + kind, mode: "read" }); } catch (e) { return; }
      const out = [];
      label[kind].className = "picked";
      label[kind].textContent = "reading...";
      setBusy(true);
      try {
        await walk(dir, dir.name, out, (n) => { label[kind].textContent = "reading... " + n + " files"; });
      } catch (e) {
        say("! Couldn't read that folder: " + e.message);
      }
      setBusy(false);
      use(kind, out);
    } else {
      waitingKind = kind;
      $("dirInput").value = "";
      $("dirInput").click();
    }
  }

  $("dirInput").addEventListener("change", (ev) => {
    const out = [];
    for (const f of ev.target.files) out.push({ path: f.webkitRelativePath || f.name, file: f });
    if (waitingKind) use(waitingKind, out);
    waitingKind = null;
  });

  // A farm is a folder like Farmer_123456789 holding a file of the same name. They are looked for
  // in the save folder that was picked and, in case a copy is kept there, in the game and mods folders.
  let farms = [];
  function findFarms() {
    const before = $("farm").value;
    farms = [];
    for (const kind of ["save", "game", "mods"]) {
      for (const e of picked[kind]) {
        const parts = e.path.split("/");
        const name = parts[parts.length - 1];
        if (parts.length < 2 || name !== parts[parts.length - 2] || !/_\d+$/.test(name)) continue;
        const dir = parts.slice(0, -1).join("/") + "/";
        farms.push({ name, files: picked[kind].filter((x) => x.path.startsWith(dir)).map((x) => ({ path: x.path.slice(dir.length - name.length - 1), file: x.file })), when: e.file.lastModified || 0 });
      }
    }
    farms.sort((x, y) => y.when - x.when);   // most recently played first
    const sel = $("farm");
    sel.innerHTML = "";
    for (const f of farms) {
      const o = document.createElement("option");
      o.value = f.name;
      o.textContent = f.name.replace(/_\d+$/, "") + (f.when ? " (last played " + new Date(f.when).toLocaleDateString() + ")" : "");
      sel.appendChild(o);
    }
    const none = document.createElement("option");
    none.value = "";
    none.textContent = "Don't bring a farm";
    sel.appendChild(none);
    if (farms.some((f) => f.name === before) || before === "" && sel.dataset.touched) sel.value = before;
    $("farmRow").hidden = !farms.length;
    const el = label.save;
    if (picked.save.length) {
      el.className = farms.length ? "picked ok" : "picked bad";
      el.textContent = farms.length ? top(picked.save) + " (" + farms.length + (farms.length === 1 ? " farm)" : " farms)")
        : "\"" + top(picked.save) + "\" has no farm inside. Pick your Saves folder.";
    } else {
      el.className = farms.length ? "picked ok" : "picked";
      el.textContent = farms.length ? "found " + farms.length + (farms.length === 1 ? " farm" : " farms") + " in your other folders" : "none";
    }
  }
  $("farm").onchange = () => { $("farm").dataset.touched = "1"; converted = false; refresh(); };

  function top(entries) { return entries.length ? entries[0].path.split("/")[0] : ""; }

  function use(kind, entries) {
    picked[kind] = entries;
    converted = false;
    const el = label[kind];
    if (kind === "game") needDll = false;
    if (!entries.length) {
      el.className = "picked";
      el.textContent = kind === "game" ? "not picked" : "none";
    } else if (kind === "game") {
      const maps = entries.filter((e) => /(^|\/)Maps\/[^/]+\.(xnb|tmx|tbin)$/i.test(e.path));
      if (!maps.length) {
        picked.game = [];
        el.className = "picked bad";
        el.textContent = "\"" + top(entries) + "\" has no Content/Maps inside. Pick your Stardew Valley folder.";
      } else {
        const packed = maps.some((e) => /\.xnb$/i.test(e.path)) && !maps.some((e) => /\.tmx$/i.test(e.path));
        const hasDll = entries.some((e) => e.path.split("/").pop() === DLL);
        needDll = packed && !hasDll;
        el.className = "picked ok";
        el.textContent = top(entries) + " (" + entries.length + " files" + (packed ? "" : ", unpacked") + ")"
          + (packed && hasDll ? ", " + DLL + " found" : "");
      }
    } else if (kind === "mods") {
      const packs = entries.filter((e) => /(^|\/)manifest\.json$/i.test(e.path)).length;
      el.className = packs ? "picked ok" : "picked bad";
      el.textContent = packs ? top(entries) + " (" + packs + " mods)" : "\"" + top(entries) + "\" has no mods inside";
      if (!packs) picked.mods = [];
    }
    findFarms();
    hint();
    refresh();
  }

  function hint() {
    if (!ready || busy) return;
    status(!picked.game.length ? "Pick your Stardew Valley folder to begin."
      : needDll && !dll ? "One more file: add " + DLL + " (step 1)." : "Ready to convert.");
  }

  $("pickDll").onclick = () => { $("dllInput").value = ""; $("dllInput").click(); };
  $("dllInput").addEventListener("change", (ev) => {
    const f = ev.target.files[0];
    if (!f) return;
    const right = /gamedata.*\.dll$/i.test(f.name);   // a copy may have been renamed a little
    dll = right ? f : null;
    $("dllName").className = right ? "picked ok" : "picked bad";
    $("dllName").textContent = right ? DLL : "That's " + f.name + ". The file needed is " + DLL + ".";
    hint();
    refresh();
  });

  $("pickGame").onclick = () => pick("game");
  $("pickMods").onclick = () => pick("mods");
  $("pickSave").onclick = () => pick("save");
  $("clearMods").onclick = () => use("mods", []);
  $("clearSave").onclick = () => use("save", []);

  // ------------------------------------------------------------ convert, then hand over
  function convert() {
    if (busy || !ready || !picked.game.length || (needDll && !dll)) return;
    $("log").textContent = "";
    state.log = [];
    state.phase = "converting";
    converted = false;
    setBusy(true);
    status("Converting...");
    // the DLL picked on its own joins the game folder, where the converter looks for it
    const game = needDll && dll ? picked.game.concat([{ path: top(picked.game) + "/" + DLL, file: dll }]) : picked.game;
    const farm = farms.find((f) => f.name === $("farm").value);
    worker.postMessage({ cmd: "convert", game, mods: picked.mods, save: farm ? farm.files : [] });
  }
  $("convert").onclick = convert;

  $("zip").onclick = () => {
    if (busy) return;
    setBusy(true);
    status("Making the zip...");
    worker.postMessage({ cmd: "zip" });
  };

  if (window.showDirectoryPicker) {
    $("saveTo").hidden = false;
    $("saveTo").onclick = async () => {
      if (busy) return;
      let dir;
      try { dir = await window.showDirectoryPicker({ id: "s3d-sd", mode: "readwrite" }); } catch (e) { return; }
      setBusy(true);
      status("Saving to " + dir.name + "...");
      worker.postMessage({ cmd: "saveTo", dir });
    };
  }

  fetch("build.txt").then((r) => (r.ok ? r.text() : "")).then((t) => {
    if (t.trim()) $("build").textContent = " · " + t.trim();
  }).catch(() => {});

  // ------------------------------------------------------------ self-test (web/test/e2e.mjs)
  // ?test=<manifest.json>: loads the listed files from this same server instead of a picked
  // folder, converts, and downloads the zip.
  async function selfTest(url) {
    const man = await (await fetch(url)).json();
    const loaded = {};
    for (const kind of ["game", "mods", "save"]) {
      const out = [];
      for (const p of man[kind] || []) {
        const r = await fetch(man.base + encodeURI(p).replace(/#/g, "%23"));
        if (!r.ok) throw new Error("test file missing: " + p);
        out.push({ path: p, file: new File([await r.blob()], p.split("/").pop()) });
      }
      loaded[kind] = out;
      use(kind, out);
    }
    while (!ready) await new Promise((r) => setTimeout(r, 200));
    if (man.script) {
      state.phase = "converting";
      worker.postMessage({ cmd: "script", game: loaded.game, src: await (await fetch(man.script)).text() });
      return;
    }
    convert();
    while (state.phase === "converting") await new Promise((r) => setTimeout(r, 300));
    if (state.phase === "done") $("zip").click();
  }

  refresh();
  startWorker();
  const test = new URLSearchParams(location.search).get("test");
  if (test) selfTest(test).catch((e) => { state.phase = "error"; say("! test: " + e.message); });
})();
