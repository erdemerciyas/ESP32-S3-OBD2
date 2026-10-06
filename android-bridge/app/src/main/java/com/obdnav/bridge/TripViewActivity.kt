package com.obdnav.bridge

import android.app.Activity
import android.content.ContentValues
import android.content.Intent
import android.graphics.Bitmap
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.os.Handler
import android.os.Looper
import android.provider.MediaStore
import android.widget.ImageView
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import java.io.File
import java.util.concurrent.Executors

/** Bir sürüşün görüntüsü: harita + rota + özet; galeriye kaydet / paylaş / sil. */
class TripViewActivity : Activity() {
    private val main = Handler(Looper.getMainLooper())
    private lateinit var img: ImageView
    private lateinit var info: TextView
    private var bmp: Bitmap? = null
    private var trip: TripRecorder.Trip? = null
    private var file: File? = null
    private var savedUri: Uri? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Ui.applyWindow(this)
        val root = Ui.column(this)
        root.addView(Ui.topBar(this, "Sürüş"))
        val col = Ui.column(this, 16)
        info = Ui.text(this, "Harita hazırlanıyor…", 14f, Ui.DIM).apply { setPadding(Ui.dp(context, 4), 0, 0, Ui.dp(context, 10)) }
        img = ImageView(this).apply {
            adjustViewBounds = true
            clipToOutline = true
            background = Ui.rounded(context, Ui.SURFACE, 18, Ui.BORDER)
        }
        col.addView(info)
        col.addView(img, Ui.lp(this, bottomDp = 14))
        col.addView(Ui.button(this, "Galeriye kaydet", Ui.Style.PRIMARY) { save(share = false) }, Ui.lp(this, bottomDp = 10))
        col.addView(Ui.row(this).apply {
            addView(Ui.button(context, "Paylaş", Ui.Style.SECONDARY) { save(share = true) },
                Ui.lp(context, 0, weight = 1f).apply { marginEnd = Ui.dp(context, 6) })
            addView(Ui.button(context, "Sil", Ui.Style.SECONDARY) { delete() }.apply { setTextColor(Ui.CRIT) },
                Ui.lp(context, 0, weight = 1f).apply { marginStart = Ui.dp(context, 6) })
        })
        root.addView(ScrollView(this).apply { addView(col) })
        setContentView(root)

        file = intent.getStringExtra(EXTRA_FILE)?.let { File(it) }
        trip = file?.let { TripRecorder.load(it) } ?: TripRecorder.current
        val t = trip ?: run { info.text = "Sürüş bulunamadı"; return }
        val app = applicationContext
        Executors.newSingleThreadExecutor().execute {
            val b = TripImage.render(app, t)
            main.post {
                bmp = b
                img.setImageBitmap(b)
                info.text = "${t.pts.size} nokta · ${t.roads.size} yol · ${t.alerts.size} uyarı"
            }
        }
    }

    private fun save(share: Boolean) {
        val b = bmp ?: return
        val t = trip ?: return
        val name = "obdnav_${t.start}.png"
        val uri = savedUri ?: try {
            if (Build.VERSION.SDK_INT >= 29) {
                val cv = ContentValues().apply {
                    put(MediaStore.Images.Media.DISPLAY_NAME, name)
                    put(MediaStore.Images.Media.MIME_TYPE, "image/png")
                    put(MediaStore.Images.Media.RELATIVE_PATH, Environment.DIRECTORY_PICTURES + "/AURA")
                }
                val u = contentResolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, cv)!!
                contentResolver.openOutputStream(u)!!.use { b.compress(Bitmap.CompressFormat.PNG, 100, it) }
                u
            } else {
                val f = File(getExternalFilesDir(Environment.DIRECTORY_PICTURES), name)
                f.outputStream().use { b.compress(Bitmap.CompressFormat.PNG, 100, it) }
                Toast.makeText(this, "Kaydedildi: ${f.path}", Toast.LENGTH_LONG).show()
                null
            }
        } catch (e: Exception) {
            Toast.makeText(this, "Kaydedilemedi: ${e.message}", Toast.LENGTH_LONG).show()
            return
        }
        savedUri = uri
        if (!share) {
            if (uri != null) Toast.makeText(this, "Galeride: Resimler/AURA", Toast.LENGTH_SHORT).show()
            return
        }
        if (uri == null) {
            Toast.makeText(this, "Paylaşım Android 10 ve üstünde", Toast.LENGTH_SHORT).show()
            return
        }
        startActivity(Intent.createChooser(Intent(Intent.ACTION_SEND).apply {
            type = "image/png"
            putExtra(Intent.EXTRA_STREAM, uri)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }, "Sürüşü paylaş"))
    }

    private fun delete() {
        val f = file ?: run { Toast.makeText(this, "Süren sürüş silinemez", Toast.LENGTH_SHORT).show(); return }
        f.delete()
        Toast.makeText(this, "Silindi", Toast.LENGTH_SHORT).show()
        finish()
    }

    companion object {
        const val EXTRA_FILE = "file"
    }
}
