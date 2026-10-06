package com.obdnav.bridge

import android.annotation.TargetApi
import android.app.Activity
import android.app.AlertDialog
import android.content.ClipData
import android.content.ContentResolver
import android.content.ContentValues
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.os.Handler
import android.os.Looper
import android.provider.MediaStore
import android.view.Gravity
import android.view.View
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import java.io.File
import java.io.RandomAccessFile
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

/** ROLL ham kayıtları (CSV, en yenisi üstte): dokun = sonuçlar; ⋯ / uzun bas = dışa aktar / paylaş / sil. */
class RollLogActivity : Activity() {
    private val main = Handler(Looper.getMainLooper())
    private lateinit var list: LinearLayout
    private var exec: ExecutorService? = null   // liste özetleri (arka planda, sırayla)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Ui.applyWindow(this)
        RollSettings.init(this)
        val root = Ui.column(this)
        root.addView(Ui.topBar(this, "ROLL kayıtları").apply {
            addView(Ui.button(context, "Ayarlar", Ui.Style.GHOST) {
                startActivity(Intent(context, RollSettingsActivity::class.java))
            })
        })
        list = Ui.column(this, 16)
        root.addView(ScrollView(this).apply { addView(list) })
        setContentView(root)
    }

    override fun onResume() {
        super.onResume()
        exec = Executors.newSingleThreadExecutor()
        reload()
    }

    override fun onPause() {
        super.onPause()
        exec?.shutdownNow()   // sıradaki özetler iptal; görüntüleyici öncelikli
        exec = null
    }

    private fun reload() {
        list.removeAllViews()
        val files = Roll.dir(this).listFiles { f -> f.name.endsWith(".csv") }?.sortedByDescending { it.name } ?: emptyList()
        // ham kaydı silinmiş sonuç dosyaları
        RollFileProvider.resDir(this).listFiles()?.forEach { r ->
            if (files.none { RollFileProvider.resultsFile(this, it).name == r.name }) r.delete()
        }
        val p = RollFmt.params(RollSettings.cfg)
        val mph = RollSettings.cfg.unit == "mph"
        for (f in files) {
            val live = f == Roll.file
            val parts = mutableListOf(Roll.sizeText(f.length()))
            durationS(f)?.let { parts += if (it >= 60) "${it / 60} dk ${it % 60} sn" else "$it sn" }
            if (live) parts += "kayıt sürüyor"
            val sum = Ui.text(this, "", 13f, Ui.PRIMARY).apply { setPadding(0, Ui.dp(context, 3), 0, 0); visibility = View.GONE }
            list.addView(item(RollFmt.title(f), parts.joinToString(" · "), sum, if (live) Ui.OK else Ui.PRIMARY, f), Ui.lp(this, bottomDp = 10))
            if (!live) summary(f, p, mph, sum)
        }
        if (files.isEmpty()) {
            list.addView(Ui.card(this).apply {
                addView(Ui.text(context, "Henüz ROLL kaydı yok", 17f, Ui.TEXT, true))
                addView(Ui.text(context, "Cihazda ROLL modu seçilip köprü açıkken ölçüm verisi otomatik kaydedilir.", 14f, Ui.DIM)
                    .apply { setPadding(0, Ui.dp(context, 4), 0, 0) })
            })
        }
    }

    /** Önbellekte varsa hemen, yoksa arka planda hesaplanıp gösterilir. */
    private fun summary(f: File, p: RollAnalysis.Params, mph: Boolean, tv: TextView) {
        fun set(s: String) { tv.text = s; tv.visibility = View.VISIBLE }
        val key = RollAnalysis.key(f, p)
        RollFmt.summaries[key]?.let { set(it); return }
        val ex = exec ?: return
        ex.execute {
            val s = try {
                (RollAnalysis.cached(f, p) ?: RollAnalysis.analyze(f, p)).let { RollFmt.summary(it, mph) }
            } catch (_: Throwable) { return@execute }
            main.post {
                RollFmt.summaries[key] = s
                if (tv.isAttachedToWindow) set(s)
            }
        }
    }

    private fun item(title: String, sub: String, sum: TextView, accent: Int, f: File): View = Ui.card(this).apply {
        addView(Ui.row(context).apply {
            addView(Ui.dot(context, accent))
            addView(Ui.column(context).apply {
                addView(Ui.text(context, title, 16f, Ui.TEXT, true))
                addView(Ui.text(context, sub, 13f, Ui.DIM).apply { setPadding(0, Ui.dp(context, 2), 0, 0) })
                addView(sum)
            }, Ui.lp(context, 0, weight = 1f))
            addView(Ui.text(context, "⋯", 22f, Ui.DIM, true).apply {
                gravity = Gravity.CENTER
                setPadding(Ui.dp(context, 14), Ui.dp(context, 4), Ui.dp(context, 4), Ui.dp(context, 8))
                isClickable = true
                setOnClickListener { menu(f) }
            })
        })
        isClickable = true
        setOnClickListener { RollRunActivity.open(this@RollLogActivity, f) }
        setOnLongClickListener { menu(f); true }
    }

    private fun menu(f: File) {
        AlertDialog.Builder(this)
            .setTitle(f.name)
            .setItems(arrayOf("Sonuçları göster", "Dışa aktar", "Paylaş", "Sil")) { _, which ->
                when (which) {
                    0 -> RollRunActivity.open(this, f)
                    1 -> export(this, f)
                    2 -> share(this, listOf(f), "ROLL kaydını paylaş")
                    else -> confirmDelete(f)
                }
            }
            .show()
    }

    private fun confirmDelete(f: File) {
        if (f == Roll.file) {
            Toast.makeText(this, "Kayıt sürerken silinemez", Toast.LENGTH_SHORT).show()
            return
        }
        AlertDialog.Builder(this)
            .setTitle("Kaydı sil")
            .setMessage("${f.name} kalıcı olarak silinsin mi?")
            .setPositiveButton("Sil") { _, _ ->
                f.delete()
                RollFileProvider.resultsFile(this, f).delete()
                Toast.makeText(this, "Silindi", Toast.LENGTH_SHORT).show()
                reload()
            }
            .setNegativeButton("Vazgeç", null)
            .show()
    }

    /** İlk / son satırdaki telefon zamanından süre (sn); dosyanın yalnız başı ve sonu okunur. */
    private fun durationS(f: File): Long? = try {
        RandomAccessFile(f, "r").use { r ->
            val len = r.length()
            fun lines(off: Long, n: Long): List<String> {
                val b = ByteArray(minOf(n, len - off).toInt())
                r.seek(off)
                r.readFully(b)
                val l = String(b, Charsets.US_ASCII).split('\n').dropLast(1)   // son satır yarım olabilir
                return if (off > 0) l.drop(1) else l                            // ilk satır yarım olabilir
            }
            val first = lines(0, 4096).firstNotNullOfOrNull(::phoneUs)
            val last = lines(maxOf(0L, len - 8192), 8192).asReversed().firstNotNullOfOrNull(::phoneUs)
            if (first != null && last != null && last > first) (last - first) / 1_000_000 else null
        }
    } catch (_: Exception) { null }

    private fun phoneUs(line: String): Long? {
        val p = line.split(',')
        return when (p[0]) {
            "G" -> p.getOrNull(2)
            "S", "M" -> p.getOrNull(1)
            else -> null
        }?.toLongOrNull()
    }

    companion object {
        private val main = Handler(Looper.getMainLooper())

        /** Herkese açık İndirilenler/AURA (API 29+, izin gerekmez); eski sürümde dahili yol gösterilir. */
        fun export(a: Activity, f: File) {
            if (Build.VERSION.SDK_INT < 29) {
                Toast.makeText(a, "Dosya: ${f.path}", Toast.LENGTH_LONG).show()
                return
            }
            val app = a.applicationContext
            Roll.afterFlush {   // io iş parçacığında: süren kayıt da güncel kopyalanır
                val msg = try {
                    copyToDownloads(app.contentResolver, f)
                    "İndirilenler/AURA klasörüne aktarıldı"
                } catch (e: Exception) {
                    "Aktarılamadı: ${e.message}"
                }
                main.post { Toast.makeText(app, msg, Toast.LENGTH_LONG).show() }
            }
        }

        @TargetApi(29)
        private fun copyToDownloads(cr: ContentResolver, f: File) {
            val cv = ContentValues().apply {
                put(MediaStore.Downloads.DISPLAY_NAME, f.name)
                put(MediaStore.Downloads.MIME_TYPE, "text/csv")
                put(MediaStore.Downloads.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS + "/AURA")
                put(MediaStore.Downloads.IS_PENDING, 1)
            }
            val u = cr.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, cv)!!
            cr.openOutputStream(u)!!.use { out -> f.inputStream().use { it.copyTo(out) } }
            cv.clear()
            cv.put(MediaStore.Downloads.IS_PENDING, 0)
            cr.update(u, cv, null, null)
        }

        /** RollFileProvider üzerinden bir ya da birden çok CSV paylaşımı. */
        fun share(a: Activity, files: List<File>, title: String) {
            Roll.afterFlush {
                main.post {
                    if (a.isFinishing) return@post
                    val uris = files.map { RollFileProvider.uri(a, it) }
                    val i = if (uris.size == 1) Intent(Intent.ACTION_SEND).putExtra(Intent.EXTRA_STREAM, uris[0])
                    else Intent(Intent.ACTION_SEND_MULTIPLE).putParcelableArrayListExtra(Intent.EXTRA_STREAM, ArrayList<Uri>(uris))
                    i.type = "text/csv"
                    i.clipData = ClipData.newRawUri(files[0].name, uris[0]).apply { uris.drop(1).forEach { addItem(ClipData.Item(it)) } }
                    i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                    a.startActivity(Intent.createChooser(i, title))
                }
            }
        }
    }
}
