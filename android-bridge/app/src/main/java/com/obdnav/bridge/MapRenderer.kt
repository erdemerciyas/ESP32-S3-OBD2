package com.obdnav.bridge

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.ColorMatrix
import android.graphics.ColorMatrixColorFilter
import android.graphics.Paint
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import kotlin.math.PI
import kotlin.math.floor
import kotlin.math.ln
import kotlin.math.sin

/**
 * OpenStreetMap karolarından harita resmi (Web Mercator, 256 px karo).
 * Karolar diskte önbelleklenir (OSM kullanım kuralı: kişisel kullanım,
 * önbellek, tanımlayıcı User-Agent). Ağ çağrısı yapar — ana iş parçacığında
 * çağrılmamalı.
 */
object MapRenderer {
    const val TILE = 256
    private const val UA = "AURA-Bridge/0.7 (personal ESP32 car display)"

    /** Dünya pikseli (zoom z'de). */
    fun world(lat: Double, lon: Double, z: Int): DoubleArray {
        val n = TILE * (1 shl z).toDouble()
        val s = sin(lat * PI / 180)
        return doubleArrayOf((lon + 180) / 360 * n, (0.5 - ln((1 + s) / (1 - s)) / (4 * PI)) * n)
    }

    /** Sınır kutusunu (pad dahil) w×h'ye sığdıran en büyük zoom. */
    fun fitZoom(minLat: Double, minLon: Double, maxLat: Double, maxLon: Double, w: Int, h: Int, pad: Int): Int {
        for (z in 17 downTo 3) {
            val a = world(maxLat, minLon, z)
            val b = world(minLat, maxLon, z)
            if (b[0] - a[0] <= w - 2 * pad && b[1] - a[1] <= h - 2 * pad) return z
        }
        return 3
    }

    /** Karanlık tema: ters çevir + 180° ton döndür (yollar açık, zemin koyu) + biraz karart. */
    private val darkPaint = Paint(Paint.FILTER_BITMAP_FLAG).apply {
        val invert = ColorMatrix(floatArrayOf(
            -1f, 0f, 0f, 0f, 255f,
            0f, -1f, 0f, 0f, 255f,
            0f, 0f, -1f, 0f, 255f,
            0f, 0f, 0f, 1f, 0f,
        ))
        val hue180 = ColorMatrix(floatArrayOf(
            -0.574f, 1.430f, 0.144f, 0f, 0f,
            0.426f, 0.430f, 0.144f, 0f, 0f,
            0.426f, 1.430f, -0.856f, 0f, 0f,
            0f, 0f, 0f, 1f, 0f,
        ))
        val cm = ColorMatrix(hue180)
        cm.preConcat(invert)
        cm.postConcat(ColorMatrix().apply { setScale(0.85f, 0.85f, 0.9f, 1f) })
        colorFilter = ColorMatrixColorFilter(cm)
    }
    private val plainPaint = Paint(Paint.FILTER_BITMAP_FLAG)

    /** Son çizimde indirilebilen / gereken karo sayısı (teşhis). */
    @Volatile
    var lastTiles = ""
        private set

    private fun tile(ctx: Context, z: Int, x: Int, y: Int): Bitmap? {
        val f = File(ctx.cacheDir, "tiles/$z/$x/$y.png")
        if (f.exists()) BitmapFactory.decodeFile(f.path)?.let { return it }
        return try {
            val c = URL("https://tile.openstreetmap.org/$z/$x/$y.png").openConnection() as HttpURLConnection
            c.setRequestProperty("User-Agent", UA)
            c.connectTimeout = 8000
            c.readTimeout = 8000
            val bytes = c.inputStream.use { it.readBytes() }
            f.parentFile?.mkdirs()
            f.writeBytes(bytes)
            BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
        } catch (e: Exception) {
            null
        }
    }

    /** Merkezi (cx, cy) dünya pikseli olan w×h harita. Eksik karolar zemin rengi kalır. */
    fun render(ctx: Context, cx: Double, cy: Double, z: Int, w: Int, h: Int, dark: Boolean): Bitmap {
        val bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
        val cv = Canvas(bmp)
        cv.drawColor(if (dark) Color.rgb(16, 22, 30) else Color.rgb(236, 232, 224))
        val left = cx - w / 2.0
        val top = cy - h / 2.0
        val n = 1 shl z
        val paint = if (dark) darkPaint else plainPaint
        var ok = 0
        var total = 0
        for (ty in floor(top / TILE).toInt()..floor((top + h - 1) / TILE).toInt()) {
            if (ty < 0 || ty >= n) continue
            for (tx in floor(left / TILE).toInt()..floor((left + w - 1) / TILE).toInt()) {
                total++
                val t = tile(ctx, z, ((tx % n) + n) % n, ty) ?: continue
                ok++
                cv.drawBitmap(t, (tx * TILE - left).toFloat(), (ty * TILE - top).toFloat(), paint)
            }
        }
        lastTiles = "$ok/$total karo"
        return bmp
    }

    fun renderAt(ctx: Context, lat: Double, lon: Double, z: Int, w: Int, h: Int, dark: Boolean): Bitmap {
        val c = world(lat, lon, z)
        return render(ctx, c[0], c[1], z, w, h, dark)
    }
}
