package com.obdnav.bridge

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.Typeface
import android.text.Layout
import android.text.StaticLayout
import android.text.TextPaint
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Sürüşün paylaşılabilir görüntüsü: karanlık OSM haritası üzerinde rota
 * (başlangıç yeşil, bitiş kırmızı, uyarılar sarı), altında özet paneli.
 * Ağdan karo indirir — arka planda çağrılmalı.
 */
object TripImage {
    private const val W = 1080
    private const val MAP_H = 1080
    private const val PANEL_H = 640

    fun render(ctx: Context, t: TripRecorder.Trip): Bitmap {
        val bmp = Bitmap.createBitmap(W, MAP_H + PANEL_H, Bitmap.Config.ARGB_8888)
        val cv = Canvas(bmp)
        cv.drawColor(Color.rgb(10, 16, 24))

        if (t.pts.isNotEmpty()) {
            val minLat = t.pts.minOf { it.lat }; val maxLat = t.pts.maxOf { it.lat }
            val minLon = t.pts.minOf { it.lon }; val maxLon = t.pts.maxOf { it.lon }
            val z = MapRenderer.fitZoom(minLat, minLon, maxLat, maxLon, W, MAP_H, 90)
            val a = MapRenderer.world(maxLat, minLon, z)
            val b = MapRenderer.world(minLat, maxLon, z)
            val cx = (a[0] + b[0]) / 2
            val cy = (a[1] + b[1]) / 2
            cv.drawBitmap(MapRenderer.render(ctx, cx, cy, z, W, MAP_H, dark = true), 0f, 0f, null)

            val ox = cx - W / 2.0
            val oy = cy - MAP_H / 2.0
            fun xy(lat: Double, lon: Double): FloatArray {
                val w = MapRenderer.world(lat, lon, z)
                return floatArrayOf((w[0] - ox).toFloat(), (w[1] - oy).toFloat())
            }
            val path = Path()
            t.pts.forEachIndexed { i, p ->
                val q = xy(p.lat, p.lon)
                if (i == 0) path.moveTo(q[0], q[1]) else path.lineTo(q[0], q[1])
            }
            val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply {
                style = Paint.Style.STROKE
                strokeJoin = Paint.Join.ROUND
                strokeCap = Paint.Cap.ROUND
            }
            stroke.color = Color.argb(160, 0, 0, 0); stroke.strokeWidth = 16f
            cv.drawPath(path, stroke)
            stroke.color = Color.rgb(0, 240, 255); stroke.strokeWidth = 9f
            cv.drawPath(path, stroke)

            val dot = Paint(Paint.ANTI_ALIAS_FLAG)
            fun marker(lat: Double, lon: Double, color: Int, r: Float) {
                val q = xy(lat, lon)
                dot.color = Color.WHITE; cv.drawCircle(q[0], q[1], r + 4, dot)
                dot.color = color; cv.drawCircle(q[0], q[1], r, dot)
            }
            for (al in t.alerts) if (al.lat != 0.0) marker(al.lat, al.lon, Color.rgb(255, 196, 0), 9f)
            marker(t.pts.first().lat, t.pts.first().lon, Color.rgb(0, 230, 118), 15f)
            marker(t.pts.last().lat, t.pts.last().lon, Color.rgb(255, 42, 42), 15f)

            val attr = TextPaint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.argb(180, 255, 255, 255); textSize = 22f }
            cv.drawText("© OpenStreetMap katkıda bulunanlar", 20f, MAP_H - 18f, attr)
        }

        drawPanel(cv, t)
        return bmp
    }

    private fun drawPanel(cv: Canvas, t: TripRecorder.Trip) {
        val top = MAP_H.toFloat()
        val pad = 48f
        val df = SimpleDateFormat("dd.MM.yyyy HH:mm", Locale("tr"))
        val tf = SimpleDateFormat("HH:mm", Locale("tr"))
        val dim = Color.rgb(140, 155, 175)

        val p = TextPaint(Paint.ANTI_ALIAS_FLAG).apply { color = dim; textSize = 34f }
        cv.drawText("${df.format(Date(t.start))} – ${tf.format(Date(t.end))}   ·   ${t.src}", pad, top + 70f, p)

        p.color = Color.rgb(0, 240, 255); p.textSize = 110f; p.typeface = Typeface.DEFAULT_BOLD
        cv.drawText("%.1f km".format(t.distM / 1000), pad, top + 190f, p)

        val mins = t.durationS / 60
        val dur = if (mins < 60) "$mins dk" else "${mins / 60} sa ${"%02d".format(mins % 60)} dk"
        val avg = if (t.durationS > 30) (t.distM / t.durationS * 3.6).toInt() else 0
        val max = t.pts.maxOfOrNull { it.spd }?.toInt()?.coerceAtLeast(0) ?: 0
        p.color = Color.WHITE; p.textSize = 40f; p.typeface = Typeface.DEFAULT
        cv.drawText("Süre $dur    Ort. $avg km/h    Maks. $max km/h", pad, top + 260f, p)

        val cams = t.alerts.count { it.kind == "cam" }
        val traffic = t.alerts.count { it.kind == "traffic" }
        val hazard = t.alerts.count { it.kind == "hazard" }
        p.color = Color.rgb(255, 196, 0); p.textSize = 34f
        cv.drawText("Uyarılar: $cams radar · $traffic trafik · $hazard tehlike", pad, top + 316f, p)

        val roads = if (t.roads.isEmpty()) "Yol adı kaydı yok" else t.roads.joinToString("  ›  ")
        val rp = TextPaint(Paint.ANTI_ALIAS_FLAG).apply { color = dim; textSize = 32f }
        val layout = StaticLayout.Builder.obtain(roads, 0, roads.length, rp, (W - 2 * pad).toInt())
            .setAlignment(Layout.Alignment.ALIGN_NORMAL)
            .setMaxLines(5)
            .setEllipsize(android.text.TextUtils.TruncateAt.END)
            .build()
        cv.save()
        cv.translate(pad, top + 350f)
        layout.draw(cv)
        cv.restore()

        val sig = TextPaint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.rgb(90, 104, 122); textSize = 26f }
        val s = "AURA  ·  Erdem ERCİYAS"
        cv.drawText(s, W - pad - sig.measureText(s), top + PANEL_H - 32f, sig)
    }
}
