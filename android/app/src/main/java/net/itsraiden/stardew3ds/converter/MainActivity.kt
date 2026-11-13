package net.itsraiden.stardew3ds.converter

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.DocumentsContract
import android.widget.Button
import android.widget.EditText
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.documentfile.provider.DocumentFile
import com.chaquo.python.Python
import com.chaquo.python.android.AndroidPlatform
import org.apache.commons.net.ftp.FTP
import org.apache.commons.net.ftp.FTPClient
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.util.zip.ZipInputStream
import org.json.JSONArray
import kotlin.concurrent.thread

/**
 * The stardew3ds converter on a phone:
 *  1. pick your game's Content folder (normal or unpacked), your mods folder (optional) and your logo (optional)
 *  2. Convert - runs the same tools/convert.py as on the PC
 *  3. Save to a folder / SD card, or Send to your 3DS over Wi-Fi (ftpd)
 *
 * Folders you pick are copied into the app's own storage first, because the
 * converter works on ordinary files. Nothing leaves your phone except what you
 * send to your own 3DS.
 */
class MainActivity : AppCompatActivity() {

    private val DLL = "StardewValley.GameData.dll"
    private val ui = Handler(Looper.getMainLooper())
    private lateinit var log: TextView
    private lateinit var scroll: ScrollView
    private lateinit var status: TextView
    private val prefs by lazy { getSharedPreferences("converter", Context.MODE_PRIVATE) }

    private var contentUri: Uri? = null
    private var modsUri: Uri? = null
    private var logoUri: Uri? = null
    private var dllUri: Uri? = null              // StardewValley.GameData.dll picked on its own
    private var contentZip: Uri? = null          // or the game files as a .zip
    private var modZips: List<Uri> = emptyList() // or mods as .zip files (each zip = one or more mods)
    @Volatile private var busy = false

    private val inDir by lazy { File(filesDir, "in") }
    private val toolsDir by lazy { File(filesDir, "tools") }     // converter from the repo
    private val gameDir by lazy { File(filesDir, "game") }       // newest stardew3ds.3dsx
    private lateinit var bar: ProgressBar
    private lateinit var barText: TextView
    private val outDir by lazy { File(filesDir, "out") }

    private val pickContent = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) { keep(uri); contentUri = uri; contentZip = null; prefs.edit().putString("content", uri.toString()).remove("contentZip").apply(); refresh() }
    }
    private val pickMods = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) { keep(uri); modsUri = uri; modZips = emptyList(); prefs.edit().putString("mods", uri.toString()).remove("modZips").apply(); refresh() }
    }
    private val pickLogo = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) {
            try { contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION) } catch (_: Exception) {}
            logoUri = uri; prefs.edit().putString("logo", uri.toString()).apply(); refresh()
        }
    }
    private val pickDll = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) {
            if (!Regex("gamedata.*\\.dll$", RegexOption.IGNORE_CASE).containsMatchIn(nameOf(uri))) {
                say("That's ${nameOf(uri)}. The file needed is $DLL (in your Stardew Valley folder, next to Content).")
            } else {
                try { contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION) } catch (_: Exception) {}
                dllUri = uri; prefs.edit().putString("dll", uri.toString()).apply(); refresh()
            }
        }
    }
    private val pickContentZip = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) {
            try { contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION) } catch (_: Exception) {}
            contentZip = uri; contentUri = null
            prefs.edit().putString("contentZip", uri.toString()).remove("content").apply(); refresh()
        }
    }
    private val pickModZips = registerForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isNotEmpty()) {
            uris.forEach { try { contentResolver.takePersistableUriPermission(it, Intent.FLAG_GRANT_READ_URI_PERMISSION) } catch (_: Exception) {} }
            // each pick adds to the list (pick several at once with a long-press, or one at a time);
            // X clears it
            modZips = (modZips + uris).distinct(); modsUri = null
            prefs.edit().putString("modZips", modZips.joinToString("\n")).remove("mods").apply(); refresh()
        }
    }
    private val pickPcSave = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) importPcSave(uri)
    }
    private val pickSaveTo = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) { keep(uri); saveTo(uri) }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        // xnbfile.py opens the bundled XNB decompressor by this path (set before Python starts)
        android.system.Os.setenv("S3D_XNB_LIB", File(applicationInfo.nativeLibraryDir, "libs3dxnb.so").absolutePath, true)
        if (!Python.isStarted()) Python.start(AndroidPlatform(this))
        log = findViewById(R.id.log)
        scroll = findViewById(R.id.scroll)
        status = findViewById(R.id.status)
        bar = findViewById(R.id.bar)
        barText = findViewById(R.id.barText)
        prefs.getString("content", null)?.let { contentUri = Uri.parse(it) }
        prefs.getString("mods", null)?.let { modsUri = Uri.parse(it) }
        prefs.getString("logo", null)?.let { logoUri = Uri.parse(it) }
        prefs.getString("contentZip", null)?.let { contentZip = Uri.parse(it) }
        prefs.getString("dll", null)?.let { dllUri = Uri.parse(it) }
        prefs.getString("modZips", null)?.let { s -> modZips = s.split("\n").filter { it.isNotBlank() }.map { Uri.parse(it) } }
        findViewById<EditText>(R.id.ip).setText(prefs.getString("ip", ""))
        findViewById<EditText>(R.id.port).setText(prefs.getString("port", "5000"))

        findViewById<Button>(R.id.pickContent).setOnClickListener { pickContent.launch(null) }
        findViewById<Button>(R.id.pickMods).setOnClickListener { pickMods.launch(null) }
        findViewById<Button>(R.id.pickContentZip).setOnClickListener { pickContentZip.launch(arrayOf("application/zip", "application/x-zip-compressed", "application/octet-stream")) }
        findViewById<Button>(R.id.pickModZips).setOnClickListener { pickModZips.launch(arrayOf("application/zip", "application/x-zip-compressed", "application/octet-stream")) }
        findViewById<Button>(R.id.pickDll).setOnClickListener { pickDll.launch(arrayOf("*/*")) }
        findViewById<Button>(R.id.clearDll).setOnClickListener { dllUri = null; prefs.edit().remove("dll").apply(); refresh() }
        findViewById<Button>(R.id.clearMods).setOnClickListener { modsUri = null; modZips = emptyList(); prefs.edit().remove("mods").remove("modZips").apply(); refresh() }
        findViewById<Button>(R.id.convert).setOnClickListener { convert() }
        findViewById<Button>(R.id.importSave).setOnClickListener {
            if (!File(outDir, "3ds/stardew3ds/data").isDirectory) say("Convert first, then bring your PC save")
            else {
                say("Pick your save: the file named like its folder (e.g. Raiden_123456789), or a zip of that folder")
                pickPcSave.launch(arrayOf("*/*"))
            }
        }
        findViewById<Button>(R.id.saveTo).setOnClickListener {
            if (!File(outDir, "3ds/stardew3ds").isDirectory) say("Convert first") else pickSaveTo.launch(null)
        }
        findViewById<Button>(R.id.send).setOnClickListener { sendToThreeDs() }
        refresh()
    }

    private fun keep(uri: Uri) {
        try {
            contentResolver.takePersistableUriPermission(
                uri, Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
        } catch (_: Exception) {}
    }

    private fun nameOf(uri: Uri?): String {
        if (uri == null) return "not picked"
        return try {
            if (DocumentsContract.isTreeUri(uri)) DocumentFile.fromTreeUri(this, uri)?.name ?: uri.toString()
            else DocumentFile.fromSingleUri(this, uri)?.name ?: uri.toString()
        } catch (_: Exception) { uri.toString() }
    }

    private fun refresh() {
        findViewById<TextView>(R.id.contentName).text = if (contentZip != null) nameOf(contentZip) else nameOf(contentUri)
        findViewById<TextView>(R.id.modsName).text = when {
            modZips.isNotEmpty() -> "${modZips.size} zip" + (if (modZips.size == 1) "" else "s")
            modsUri == null -> "none (no SVE / Ridgeside)"
            else -> nameOf(modsUri)
        }
        findViewById<TextView>(R.id.dllName).text = if (dllUri != null) DLL else "not picked"
        val ready = File(outDir, "3ds/stardew3ds").isDirectory
        status.text = when {
            busy -> "Working..."
            ready -> "Converted files are ready to save or send."
            else -> "Pick your game's Content folder (or a zip of it), then Convert."
        }
    }

    private fun say(s: String) { ui.post { appendLog(s) } }

    /** Progress bar: overall 0..100 plus what's happening right now. */
    private fun progress(pct: Int, what: String) {
        ui.post {
            bar.isIndeterminate = pct < 0
            if (pct >= 0) bar.progress = pct.coerceIn(0, 100)
            barText.text = what
        }
    }

    // converter log lines -> where we are in the conversion (40..95 of the bar)
    private val stages = listOf(
        "Mods..." to 42, "linking base content" to 44, "Maps..." to 62, "maps converted" to 70,
        "Seasonal maps" to 72, "Data..." to 76, "Villagers..." to 82, "Events..." to 86,
        "Fonts..." to 88, "Textures" to 90, "Done in" to 96)

    private fun convertProgress(line: String) {
        for ((key, pct) in stages)
            if (line.contains(key)) { progress(pct, line.trim().take(60)); return }
    }

    private fun appendLog(s: String) {
        log.append(s + "\n")
        if (log.lineCount > 4000) log.text = log.text.toString().takeLast(60000)
        scroll.post { scroll.scrollTo(0, log.bottom) }   // not fullScroll: that steals focus from the IP and port boxes
    }

    // While converting or sending, the phone must not doze: the screen stays on, and if it's locked by hand
    // the CPU and Wi-Fi are held awake so the transfer carries on.
    private var wakeLock: android.os.PowerManager.WakeLock? = null
    private var wifiLock: android.net.wifi.WifiManager.WifiLock? = null
    private fun stayAwake(on: Boolean) {
        try {
            if (on) {
                window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                if (wakeLock == null) wakeLock = (getSystemService(Context.POWER_SERVICE) as android.os.PowerManager)
                    .newWakeLock(android.os.PowerManager.PARTIAL_WAKE_LOCK, "stardew3ds:work")
                if (wifiLock == null) wifiLock = (applicationContext.getSystemService(Context.WIFI_SERVICE) as android.net.wifi.WifiManager)
                    .createWifiLock(android.net.wifi.WifiManager.WIFI_MODE_FULL_HIGH_PERF, "stardew3ds:send")
                if (wakeLock?.isHeld == false) wakeLock?.acquire(60 * 60 * 1000L)
                if (wifiLock?.isHeld == false) wifiLock?.acquire()
            } else {
                window.clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                if (wakeLock?.isHeld == true) wakeLock?.release()
                if (wifiLock?.isHeld == true) wifiLock?.release()
            }
        } catch (_: Exception) {}
    }

    private fun setBusy(b: Boolean) { ui.post {
        busy = b
        stayAwake(b)
        listOf(R.id.convert, R.id.saveTo, R.id.send, R.id.pickContent, R.id.pickMods, R.id.pickDll)
            .forEach { findViewById<Button>(it).isEnabled = !b }
        refresh()
    } }

    // ---------------------------------------------------------------- convert

    private var redo = false   // a second tap on Convert redoes it even when nothing changed
    // What a conversion depends on: which files you picked, and the converter's own code (not the build number,
    // so a new build that only changes the game doesn't make you convert again).
    private fun fingerprint(): String {
        val md = java.security.MessageDigest.getInstance("MD5")
        (toolsDir.listFiles() ?: emptyArray()).filter { it.isFile }.sortedBy { it.name }.forEach { md.update(it.name.toByteArray()); md.update(it.readBytes()) }
        val tools = md.digest().joinToString("") { "%02x".format(it) }
        return listOf(contentUri, contentZip, modsUri, modZips.joinToString(","), dllUri, tools).joinToString("|")
    }

    private fun convert() {
        if (contentUri == null && contentZip == null) return say("Pick your Content folder or zip first")
        if (busy) return
        setBusy(true)
        log.text = ""
        thread(name = "convert") {
            try {
                val contentDir = File(inDir, "content")
                val modsDir = File(inDir, "mods")
                updateFromRepo()
                // Same game files, same mods, same build as last time, and the result is still here: nothing to redo.
                val print = fingerprint()
                if (!redo && print == prefs.getString("converted", "") && File(outDir, "3ds/stardew3ds/data").isDirectory) {
                    redo = true
                    File(gameDir, "stardew3ds.3dsx").takeIf { it.isFile }?.copyTo(File(outDir, "3ds/stardew3ds.3dsx"), overwrite = true)
                    say("Nothing has changed since your last convert, so your files are ready: Save or Send them.")
                    say("(Tap Convert again if you want to redo it anyway.)")
                    progress(100, "Already converted - save or send it")
                    return@thread
                }
                redo = false
                prefs.edit().remove("converted").apply()
                progress(20, "Copying your game files...")
                say("Copying your game files into the app...")
                // a zip is only unpacked again when it's a different zip (or a different size) from last time
                fun stampOf(uris: List<Uri>) = uris.joinToString("|") { u ->
                    val size = try { contentResolver.openFileDescriptor(u, "r")?.use { it.statSize } ?: -1L } catch (_: Exception) { -1L }
                    "$u:$size"
                }
                if (contentZip != null) {
                    val stamp = stampOf(listOf(contentZip!!))
                    if (stamp == prefs.getString("contentStamp", "") && findContentRoot(contentDir) != null) {
                        say("Your game files are already unpacked from last time.")
                    } else {
                        contentDir.deleteRecursively()
                        prefs.edit().remove("contentStamp").apply()
                        progress(-1, "Unzipping your game files...")
                        unzipIn(contentZip!!, contentDir, "content")
                        prefs.edit().putString("contentStamp", stamp).apply()
                    }
                } else {
                    prefs.edit().remove("contentStamp").apply()
                    copyTreeIn(contentUri!!, contentDir, "content")
                }
                val hasMods = modsUri != null || modZips.isNotEmpty()
                val modStamp = if (modZips.isNotEmpty()) stampOf(modZips) else ""
                if (modZips.isNotEmpty() && modStamp == prefs.getString("modStamp", "") && modsDir.isDirectory) {
                    say("Your mods are already unpacked from last time.")
                } else {
                    modsDir.deleteRecursively()
                    prefs.edit().remove("modStamp").apply()
                    if (modsUri != null) {
                        say("Copying your mods...")
                        copyTreeIn(modsUri!!, modsDir, "mods")
                    } else if (modZips.isNotEmpty()) {
                        say("Unzipping your mods...")
                        progress(32, "Unzipping your mods...")
                        modZips.forEachIndexed { i, z -> unzipIn(z, File(modsDir, "mod$i"), nameOf(z)) }
                        prefs.edit().putString("modStamp", modStamp).apply()
                    }
                }
                val logoPath = ""   // the logo is built into the game now
                outDir.deleteRecursively()
                outDir.mkdirs()
                // the folder you pick may be the one *containing* Content (unpacked)
                val root = findContentRoot(contentDir) ?: run {
                    say("! Couldn't find a Maps folder in what you picked - pick your game's Content folder (normal or unpacked)")
                    return@thread
                }
                // the game DLL picked on its own goes inside Content, where the converter looks for it
                dllUri?.let { u ->
                    try {
                        contentResolver.openInputStream(u)!!.use { i -> File(root, DLL).outputStream().use { i.copyTo(it) } }
                        say("Added $DLL")
                    } catch (e: Exception) { say("! Couldn't read the $DLL you picked: ${e.message}") }
                }
                say("Converting (this takes a while with SVE and Ridgeside)...")
                progress(40, "Converting...")
                val cb = object : LineSink { override fun onLine(line: String) { say(line); convertProgress(line) } }
                val code = Python.getInstance().getModule("bridge").callAttr(
                    "run", root.absolutePath, if (hasMods) modsDir.absolutePath else "",
                    outDir.absolutePath, logoPath, cb, toolsDir.absolutePath).toInt()
                // the newest game goes along with the converted files
                val game = File(gameDir, "stardew3ds.3dsx")
                if (code == 0 && game.isFile) game.copyTo(File(outDir, "3ds/stardew3ds.3dsx"), overwrite = true)
                if (code == 0) prefs.edit().putString("converted", print).apply()
                say(if (code == 0) "Done!" else "Converter stopped (code $code)")
                progress(if (code == 0) 100 else 0, if (code == 0) "Done - save or send it" else "Stopped (see log)")
            } catch (e: Throwable) {
                say("! ${e.javaClass.simpleName}: ${e.message}")
            } finally {
                setBusy(false)
            }
        }
    }

    // A PC save (Saves/<Name_id>/<Name_id>, or that folder zipped) -> import.txt for the 3DS
    private fun importPcSave(uri: Uri) {
        if (busy) return
        setBusy(true)
        thread(name = "import") {
            try {
                val dir = File(cacheDir, "pcsave")
                dir.deleteRecursively()
                dir.mkdirs()
                val name = nameOf(uri).substringAfterLast('/').replace(':', '_').ifBlank { "save" }
                if (name.endsWith(".zip", ignoreCase = true)) unzipIn(uri, dir, "your save")
                else contentResolver.openInputStream(uri)!!.use { inp -> File(dir, name).outputStream().use { inp.copyTo(it) } }
                val files = dir.walkTopDown().filter {
                    it.isFile && !it.name.startsWith("SaveGameInfo") && !it.name.endsWith("_old") && !it.name.endsWith(".zip")
                }.toList()
                val save = files.firstOrNull { it.name == it.parentFile?.name } ?: files.maxByOrNull { it.length() }
                if (save == null) { say("! No save file in what you picked"); return@thread }
                say("Reading ${save.name}...")
                val cb = object : LineSink { override fun onLine(line: String) { say(line) } }
                val code = Python.getInstance().getModule("bridge").callAttr(
                    "import_save", save.absolutePath, File(outDir, "3ds/stardew3ds").absolutePath, cb, toolsDir.absolutePath).toInt()
                say(if (code == 0) "Your PC farm is ready. Save or send it to your 3DS, then start the game and choose Load. Farms already on the 3DS are kept."
                    else "Couldn't read that save (code $code)")
            } catch (e: Throwable) {
                say("! ${e.javaClass.simpleName}: ${e.message}")
            } finally {
                setBusy(false)
            }
        }
    }

    interface LineSink { fun onLine(line: String) }

    // ---------------------------------------------------------------- updates from the repo
    private val repo = "Raidenplays/stardew-3ds"
    private val converterFiles = listOf("convert.py", "cp.py", "sjson.py", "tbin.py", "tmxmap.py", "xnb.py", "layer.py",
        "xnbfile.py", "xnbunpack.py", "netschema.py", "saveimport.py")

    private fun fetch(url: String, to: File? = null, onBytes: ((Long, Long) -> Unit)? = null): String? {
        val c = URL(url).openConnection() as HttpURLConnection
        c.connectTimeout = 10000
        c.readTimeout = 30000
        c.setRequestProperty("User-Agent", "stardew3ds-converter")
        c.instanceFollowRedirects = true
        try {
            if (c.responseCode !in 200..299) throw IllegalStateException("HTTP ${c.responseCode} for $url")
            val total = c.contentLengthLong
            c.inputStream.use { ins ->
                if (to == null) return ins.bufferedReader().readText()
                val tmp = File(to.path + ".part")
                FileOutputStream(tmp).use { o ->
                    val buf = ByteArray(1 shl 16)
                    var got = 0L
                    while (true) {
                        val n = ins.read(buf)
                        if (n < 0) break
                        o.write(buf, 0, n)
                        got += n
                        onBytes?.invoke(got, total)
                    }
                }
                tmp.renameTo(to)
                return null
            }
        } finally {
            c.disconnect()
        }
    }

    /** Downloads the newest build's converter code and stardew3ds.3dsx - only when
     *  the app doesn't have them yet. */
    private fun updateFromRepo() {
        val have = prefs.getString("build", "") ?: ""
        val haveFiles = File(toolsDir, "convert.py").isFile && File(gameDir, "stardew3ds.3dsx").isFile
        progress(2, "Checking GitHub for a newer build...")
        try {
            val rel = JSONArray(fetch("https://api.github.com/repos/$repo/releases?per_page=1")!!).getJSONObject(0)
            if (haveFiles && rel.getString("tag_name") == have) {
                say("Converter and game are up to date ($have)")
                progress(18, "Up to date ($have)")
                return
            }
            val tag = rel.getString("tag_name")
            say("Getting $tag from GitHub...")
            toolsDir.mkdirs()
            gameDir.mkdirs()
            converterFiles.forEachIndexed { i, f ->
                progress(4 + i * 8 / converterFiles.size, "Converter: $f")
                fetch("https://raw.githubusercontent.com/$repo/$tag/tools/$f", File(toolsDir, f))
            }
            val assets = rel.getJSONArray("assets")
            for (i in 0 until assets.length()) {
                val a = assets.getJSONObject(i)
                if (a.getString("name") != "stardew3ds.3dsx") continue
                fetch(a.getString("browser_download_url"), File(gameDir, "stardew3ds.3dsx")) { got, total ->
                    val pct = if (total > 0) 12 + (got * 6 / total).toInt() else 14
                    progress(pct, "Game: ${got / 1024} KB" + if (total > 0) " of ${total / 1024} KB" else "")
                }
            }
            prefs.edit().putString("build", tag).apply()
            say("Updated to $tag")
            progress(18, "Updated to $tag")
        } catch (e: Exception) {
            // offline or GitHub hiccup: use what we have (or the converter built into the app)
            say("Couldn't check GitHub (${e.message}); using the converter you already have")
            progress(18, "Using the converter you already have")
        }
    }

    /** Unpacks a picked .zip into app storage (paths that try to escape the folder are skipped). */
    private fun unzipIn(zip: Uri, dest: File, label: String) {
        dest.mkdirs()
        val base = dest.canonicalPath + File.separator
        var n = 0
        contentResolver.openInputStream(zip)?.use { raw ->
            ZipInputStream(raw.buffered(1 shl 16)).use { z ->
                while (true) {
                    val e = z.nextEntry ?: break
                    val target = File(dest, e.name.replace('\\', '/'))
                    if (!target.canonicalPath.startsWith(base)) continue
                    if (e.isDirectory) target.mkdirs()
                    else {
                        target.parentFile?.mkdirs()
                        FileOutputStream(target).use { o -> z.copyTo(o, 1 shl 16) }
                        if (++n % 500 == 0) say("  $label: $n files...")
                    }
                }
            }
        }
        say("  $label: $n files")
    }

    private fun findContentRoot(dir: File): File? {
        if (File(dir, "Maps").isDirectory) return dir
        dir.listFiles()?.filter { it.isDirectory }?.forEach { sub ->
            if (File(sub, "Maps").isDirectory) return sub
        }
        return null
    }

    /** Copies a picked folder into app storage, skipping files that are already there and unchanged. */
    private fun copyTreeIn(tree: Uri, dest: File, label: String) {
        val root = DocumentFile.fromTreeUri(this, tree) ?: throw IllegalStateException("can't open $label folder")
        var copied = 0
        var skipped = 0
        fun walk(doc: DocumentFile, into: File) {
            into.mkdirs()
            for (child in doc.listFiles()) {
                val name = child.name ?: continue
                val target = File(into, name)
                if (child.isDirectory) {
                    walk(child, target)
                } else if (target.exists() && target.length() == child.length() &&
                           target.lastModified() >= child.lastModified()) {
                    skipped++
                } else {
                    contentResolver.openInputStream(child.uri)?.use { i ->
                        FileOutputStream(target).use { o -> i.copyTo(o, 1 shl 16) }
                    }
                    copied++
                    if (copied % 200 == 0) say("  $label: $copied files...")
                }
            }
        }
        walk(root, dest)
        say("  $label: $copied copied, $skipped unchanged")
    }

    // ---------------------------------------------------------------- save / send

    private fun saveTo(tree: Uri) {
        if (busy) return
        setBusy(true)
        thread(name = "save") {
            try {
                val root = DocumentFile.fromTreeUri(this, tree) ?: throw IllegalStateException("can't open that folder")
                say("Saving to ${root.name}/3ds/stardew3ds ...")
                val total = File(outDir, "3ds").walkTopDown().count { it.isFile }.coerceAtLeast(1)
                var n = 0
                fun put(src: File, into: DocumentFile) {
                    for (f in src.listFiles() ?: emptyArray()) {
                        if (f.isDirectory) {
                            val d = into.findFile(f.name) ?: into.createDirectory(f.name) ?: continue
                            put(f, d)
                        } else {
                            into.findFile(f.name)?.delete()
                            val doc = into.createFile("application/octet-stream", f.name) ?: continue
                            contentResolver.openOutputStream(doc.uri)?.use { o -> FileInputStream(f).use { it.copyTo(o, 1 shl 16) } }
                            ++n
                            if (n % 20 == 0) progress(n * 100 / total, "Saving: $n of $total files")
                        }
                    }
                }
                val threeDs = root.findFile("3ds") ?: root.createDirectory("3ds")!!
                val dest = threeDs.findFile("stardew3ds") ?: threeDs.createDirectory("stardew3ds")!!
                put(File(outDir, "3ds/stardew3ds"), dest)
                File(outDir, "3ds/stardew3ds.3dsx").takeIf { it.isFile }?.let { g ->
                    threeDs.findFile(g.name)?.delete()
                    threeDs.createFile("application/octet-stream", g.name)?.let { doc ->
                        contentResolver.openOutputStream(doc.uri)?.use { o -> FileInputStream(g).use { it.copyTo(o) } }
                        n++
                    }
                }
                progress(100, "Saved $n files")
                say("Saved $n files (game included). Your save and backups on the card were left alone.")
            } catch (e: Throwable) {
                say("! ${e.javaClass.simpleName}: ${e.message}")
            } finally {
                setBusy(false)
            }
        }
    }

    /** Uploads to sdmc:/3ds/stardew3ds/ on a 3DS running ftpd (same Wi-Fi). */
    private fun sendToThreeDs() {
        if (busy) return
        val ip = findViewById<EditText>(R.id.ip).text.toString().trim()
        val port = findViewById<EditText>(R.id.port).text.toString().trim().toIntOrNull() ?: 5000
        if (ip.isEmpty()) return say("Type the IP address ftpd shows on your 3DS")
        if (!File(outDir, "3ds/stardew3ds").isDirectory) return say("Convert first")
        prefs.edit().putString("ip", ip).putString("port", port.toString()).apply()
        setBusy(true)
        thread(name = "ftp") {
            var ftp = FTPClient()
            fun connect() {
                try { ftp.disconnect() } catch (_: Exception) {}
                ftp = FTPClient()
                ftp.connectTimeout = 8000
                ftp.connect(ip, port)
                ftp.login("anonymous", "")
                ftp.enterLocalPassiveMode()
                ftp.setFileType(FTP.BINARY_FILE_TYPE)
                ftp.soTimeout = 20000
                ftp.setDataTimeout(20000)
            }
            try {
                say("Connecting to $ip:$port ...")
                connect()
                val total = File(outDir, "3ds").walkTopDown().count { it.isFile }.coerceAtLeast(1)
                var n = 0
                // The link can drop mid-way (phone dozing, 3DS Wi-Fi hiccup): reconnect and carry on from that file.
                fun <T> retrying(what: String, job: () -> T): T {
                    var last: Throwable? = null
                    for (attempt in 1..6) {
                        try { return job() } catch (e: java.io.IOException) {
                            last = e
                            say("Connection dropped ($what). Reconnecting, try $attempt of 6...")
                            Thread.sleep(1500L * attempt)
                            try { connect() } catch (_: Exception) {}
                        }
                    }
                    throw last ?: java.io.IOException("gave up")
                }
                fun mk(path: String) {
                    var cur = ""
                    for (part in path.trim('/').split('/')) {
                        cur += "/$part"
                        val dir = cur
                        retrying(dir) { ftp.makeDirectory(dir) }
                    }
                }
                fun store(f: File, remote: String) = retrying(f.name) {
                    val ok = FileInputStream(f).use { ftp.storeFile(remote, it) }
                    if (!ok) throw java.io.IOException("3DS refused ${f.name}: ${ftp.replyString.trim()}")
                }
                fun put(src: File, remote: String) {
                    mk(remote)
                    for (f in src.listFiles() ?: emptyArray()) {
                        if (f.isDirectory) put(f, "$remote/${f.name}")
                        else {
                            store(f, "$remote/${f.name}")
                            ++n
                            if (n % 10 == 0) progress(n * 100 / total, "Sending: $n of $total files")
                        }
                    }
                }
                put(File(outDir, "3ds/stardew3ds"), "/3ds/stardew3ds")
                File(outDir, "3ds/stardew3ds.3dsx").takeIf { it.isFile }?.let { g ->
                    store(g, "/3ds/${g.name}")
                    n++
                }
                progress(100, "Sent $n files")
                say("Sent $n files to your 3DS (game included).")
            } catch (e: Throwable) {
                say("! Couldn't send: ${e.message}. Is ftpd running and on the same Wi-Fi?")
            } finally {
                try { ftp.logout(); ftp.disconnect() } catch (_: Exception) {}
                setBusy(false)
            }
        }
    }
}
