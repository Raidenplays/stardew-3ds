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
import java.util.zip.ZipInputStream
import kotlin.concurrent.thread

/**
 * The stardew3ds converter on a phone:
 *  1. pick your "Content (unpacked)" folder, your mods folder (optional) and your logo (optional)
 *  2. Convert - runs the same tools/convert.py as on the PC
 *  3. Save to a folder / SD card, or Send to your 3DS over Wi-Fi (ftpd)
 *
 * Folders you pick are copied into the app's own storage first, because the
 * converter works on ordinary files. Nothing leaves your phone except what you
 * send to your own 3DS.
 */
class MainActivity : AppCompatActivity() {

    private val ui = Handler(Looper.getMainLooper())
    private lateinit var log: TextView
    private lateinit var scroll: ScrollView
    private lateinit var status: TextView
    private val prefs by lazy { getSharedPreferences("converter", Context.MODE_PRIVATE) }

    private var contentUri: Uri? = null
    private var modsUri: Uri? = null
    private var logoUri: Uri? = null
    private var contentZip: Uri? = null          // or the game files as a .zip
    private var modZips: List<Uri> = emptyList() // or mods as .zip files (each zip = one or more mods)
    @Volatile private var busy = false

    private val inDir by lazy { File(filesDir, "in") }
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
            modZips = uris; modsUri = null
            prefs.edit().putString("modZips", uris.joinToString("\n")).remove("mods").apply(); refresh()
        }
    }
    private val pickSaveTo = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) { keep(uri); saveTo(uri) }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        if (!Python.isStarted()) Python.start(AndroidPlatform(this))
        log = findViewById(R.id.log)
        scroll = findViewById(R.id.scroll)
        status = findViewById(R.id.status)
        prefs.getString("content", null)?.let { contentUri = Uri.parse(it) }
        prefs.getString("mods", null)?.let { modsUri = Uri.parse(it) }
        prefs.getString("logo", null)?.let { logoUri = Uri.parse(it) }
        prefs.getString("contentZip", null)?.let { contentZip = Uri.parse(it) }
        prefs.getString("modZips", null)?.let { s -> modZips = s.split("\n").filter { it.isNotBlank() }.map { Uri.parse(it) } }
        findViewById<EditText>(R.id.ip).setText(prefs.getString("ip", ""))
        findViewById<EditText>(R.id.port).setText(prefs.getString("port", "5000"))

        findViewById<Button>(R.id.pickContent).setOnClickListener { pickContent.launch(null) }
        findViewById<Button>(R.id.pickMods).setOnClickListener { pickMods.launch(null) }
        findViewById<Button>(R.id.pickContentZip).setOnClickListener { pickContentZip.launch(arrayOf("application/zip", "application/x-zip-compressed", "application/octet-stream")) }
        findViewById<Button>(R.id.pickModZips).setOnClickListener { pickModZips.launch(arrayOf("application/zip", "application/x-zip-compressed", "application/octet-stream")) }
        findViewById<Button>(R.id.clearMods).setOnClickListener { modsUri = null; modZips = emptyList(); prefs.edit().remove("mods").remove("modZips").apply(); refresh() }
        findViewById<Button>(R.id.convert).setOnClickListener { convert() }
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
        val ready = File(outDir, "3ds/stardew3ds").isDirectory
        status.text = when {
            busy -> "Working..."
            ready -> "Converted files are ready to save or send."
            else -> "Pick your Content (unpacked) folder, then Convert."
        }
    }

    private fun say(s: String) { ui.post { appendLog(s) } }

    private fun appendLog(s: String) {
        log.append(s + "\n")
        if (log.lineCount > 4000) log.text = log.text.toString().takeLast(60000)
        scroll.post { scroll.fullScroll(ScrollView.FOCUS_DOWN) }
    }

    private fun setBusy(b: Boolean) { ui.post {
        busy = b
        listOf(R.id.convert, R.id.saveTo, R.id.send, R.id.pickContent, R.id.pickMods)
            .forEach { findViewById<Button>(it).isEnabled = !b }
        refresh()
    } }

    // ---------------------------------------------------------------- convert

    private fun convert() {
        if (contentUri == null && contentZip == null) return say("Pick your Content (unpacked) folder or zip first")
        if (busy) return
        setBusy(true)
        log.text = ""
        thread(name = "convert") {
            try {
                val contentDir = File(inDir, "content")
                val modsDir = File(inDir, "mods")
                say("Copying your game files into the app...")
                if (contentZip != null) {
                    contentDir.deleteRecursively()
                    unzipIn(contentZip!!, contentDir, "content")
                } else {
                    copyTreeIn(contentUri!!, contentDir, "content")
                }
                val hasMods = modsUri != null || modZips.isNotEmpty()
                modsDir.deleteRecursively()
                if (modsUri != null) {
                    say("Copying your mods...")
                    copyTreeIn(modsUri!!, modsDir, "mods")
                } else if (modZips.isNotEmpty()) {
                    say("Unzipping your mods...")
                    modZips.forEachIndexed { i, z -> unzipIn(z, File(modsDir, "mod$i"), nameOf(z)) }
                }
                val logoPath = ""   // the logo is built into the game now
                outDir.deleteRecursively()
                outDir.mkdirs()
                // the folder you pick may be the one *containing* Content (unpacked)
                val root = findContentRoot(contentDir) ?: run {
                    say("! Couldn't find a Maps folder in what you picked - pick the 'Content (unpacked)' folder")
                    return@thread
                }
                say("Converting (this takes a while with SVE and Ridgeside)...")
                val cb = object : LineSink { override fun onLine(line: String) = say(line) }
                val code = Python.getInstance().getModule("bridge").callAttr(
                    "run", root.absolutePath, if (hasMods) modsDir.absolutePath else "",
                    outDir.absolutePath, logoPath, cb).toInt()
                say(if (code == 0) "Done!" else "Converter stopped (code $code)")
            } catch (e: Throwable) {
                say("! ${e.javaClass.simpleName}: ${e.message}")
            } finally {
                setBusy(false)
            }
        }
    }

    interface LineSink { fun onLine(line: String) }

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
                            if (++n % 100 == 0) say("  $n files...")
                        }
                    }
                }
                val threeDs = root.findFile("3ds") ?: root.createDirectory("3ds")!!
                val dest = threeDs.findFile("stardew3ds") ?: threeDs.createDirectory("stardew3ds")!!
                put(File(outDir, "3ds/stardew3ds"), dest)
                say("Saved $n files. Your save and backups on the card were left alone.")
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
            val ftp = FTPClient()
            try {
                say("Connecting to $ip:$port ...")
                ftp.connectTimeout = 8000
                ftp.connect(ip, port)
                ftp.login("anonymous", "")
                ftp.enterLocalPassiveMode()
                ftp.setFileType(FTP.BINARY_FILE_TYPE)
                var n = 0
                fun mk(path: String) {
                    var cur = ""
                    for (part in path.trim('/').split('/')) {
                        cur += "/$part"
                        ftp.makeDirectory(cur)
                    }
                }
                fun put(src: File, remote: String) {
                    mk(remote)
                    for (f in src.listFiles() ?: emptyArray()) {
                        if (f.isDirectory) put(f, "$remote/${f.name}")
                        else {
                            FileInputStream(f).use { ftp.storeFile("$remote/${f.name}", it) }
                            if (++n % 50 == 0) say("  $n files...")
                        }
                    }
                }
                put(File(outDir, "3ds/stardew3ds"), "/3ds/stardew3ds")
                say("Sent $n files to your 3DS.")
            } catch (e: Throwable) {
                say("! Couldn't send: ${e.message}. Is ftpd running and on the same Wi-Fi?")
            } finally {
                try { ftp.logout(); ftp.disconnect() } catch (_: Exception) {}
                setBusy(false)
            }
        }
    }
}
