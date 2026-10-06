package com.obdnav.bridge

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.graphics.DashPathEffect
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.Typeface
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.hypot
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow

/**
 * ROLL hız grafiği (üst) + boyuna g şeridi (alt), ortak zaman ekseni.
 * Dokun / yatay sürükle: imleç (t, hız, g). İki parmak: yakınlaştır / kaydır.
 * Çift dokunuş: tüm kayıt. Yoğun veride piksel sütunu başına min/maks çizilir.
 */
@SuppressLint("ViewConstructor")
class RollChart(ctx: Context, private val r: RollAnalysis.Result, private val mph: Boolean) : View(ctx) {
    private val k = if (mph) 1 / 1.609344 else 1.0
    private val unit = if (mph) "mph" else "km/h"
    private val full0 = 0.0
    private val full1 = max(r.duration, 1.0)
    private var x0 = full0
    private var x1 = full1
    private var cursor: Double? = null          // s (t0'a göre)

    private val vMax = run {
        var m = r.maxKmh
        for (x in r.gps.v) m = max(m, x)
        for (x in r.obd.v) m = max(m, x)
        max(m * k * 1.08, 20.0)
    }
    private val gMax = max(0.3, ceil(max(r.peakG, -r.minG) * 10 + 0.5) / 10)

    private fun d(v: Number) = Ui.dp(context, v).toFloat()

    private val grid = Paint().apply { color = Ui.BORDER; strokeWidth = 1f }
    private val axis = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Ui.DIM; textSize = d(10) }
    private val line = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Ui.PRIMARY; style = Paint.Style.STROKE; strokeWidth = d(2); strokeJoin = Paint.Join.ROUND
    }
    private val glow = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0x3300E5FF; style = Paint.Style.STROKE; strokeWidth = d(6); strokeJoin = Paint.Join.ROUND
    }
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val gLine = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Ui.WARN; style = Paint.Style.STROKE; strokeWidth = d(1.3) }
    private val gpsDot = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Ui.TEXT }
    private val obdDot = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Ui.ACCENT }
    private val launch = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Ui.OK; strokeWidth = d(1.2); pathEffect = DashPathEffect(floatArrayOf(d(4), d(3)), 0f)
    }
    private val band = Paint().apply { color = 0x1400E5FF }
    private val bandText = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Ui.PRIMARY; textSize = d(10); typeface = Typeface.create("sans-serif-medium", Typeface.BOLD)
    }
    private val cur = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xAAEEF3F8.toInt(); strokeWidth = d(1) }
    private val readout = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Ui.TEXT; textSize = d(12.5f); typeface = Typeface.create("sans-serif-medium", Typeface.BOLD)
    }
    private val readoutBg = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Ui.SURFACE_HI }
    private val hint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Ui.DIM; textSize = d(11.5f) }
    private val path = Path()
    private val area = Path()
    private val rect = RectF()

    // çizim alanı (onDraw'da hesaplanır)
    private var left = 0f
    private var right = 0f
    private var top1 = 0f
    private var bot1 = 0f
    private var top2 = 0f
    private var bot2 = 0f

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        setMeasuredDimension(MeasureSpec.getSize(widthMeasureSpec), Ui.dp(context, 320))
    }

    private fun xOf(t: Double) = (left + (t - x0) / (x1 - x0) * (right - left)).toFloat()
    private fun tOf(x: Float) = x0 + (x - left) / (right - left) * (x1 - x0)
    private fun yV(v: Double) = (bot1 - v * k / vMax * (bot1 - top1)).toFloat()
    private fun yG(g: Double) = ((top2 + bot2) / 2 - g / gMax * (bot2 - top2) / 2).toFloat()

    private fun niceStep(range: Double, count: Int): Double {
        val raw = range / count
        val mag = 10.0.pow(floor(log10(raw)))
        val n = raw / mag
        return mag * when { n < 1.5 -> 1.0; n < 3.5 -> 2.0; n < 7.5 -> 5.0; else -> 10.0 }
    }

    private fun lb(ts: DoubleArray, x: Double): Int {
        var lo = 0
        var hi = ts.size
        while (lo < hi) { val m = (lo + hi) ushr 1; if (ts[m] < x) lo = m + 1 else hi = m }
        return lo
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        left = d(36); right = w - d(10)
        top1 = d(30); bot1 = top1 + (h - d(30) - d(22)) * 0.70f
        top2 = bot1 + d(12); bot2 = h - d(22)
        fill.shader = LinearGradient(0f, top1, 0f, bot1, 0x3300E5FF, 0x0000E5FF, Shader.TileMode.CLAMP)
    }

    override fun onDraw(c: Canvas) {
        val t0 = r.t0

        // koşu bantları
        for (run in r.runs) {
            val a = xOf(run.start - t0).coerceIn(left, right)
            val b = xOf(run.end - t0).coerceIn(left, right)
            if (b - a < 1) continue
            c.drawRect(a, top1, b, bot2, band)
            c.drawText("${run.no}", a + d(4), top1 + d(12), bandText)
        }

        // ızgara: hız
        val vs = niceStep(vMax, 4)
        var v = 0.0
        while (v <= vMax) {
            val y = yV(v / k)
            c.drawLine(left, y, right, y, grid)
            c.drawText("%.0f".format(v), d(4), y + d(3.5f), axis)
            v += vs
        }
        // ızgara: g
        for (g in listOf(-gMax / 2, 0.0, gMax / 2)) {
            val y = yG(g)
            c.drawLine(left, y, right, y, grid)
            c.drawText(if (g == 0.0) "0 g" else "%+.1f".format(g), d(4), y + d(3.5f), axis)
        }
        c.drawLine(left, bot1, right, bot1, grid)
        // zaman ekseni
        val ts = niceStep(x1 - x0, 5)
        var t = ceil(x0 / ts) * ts
        while (t <= x1) {
            val x = xOf(t)
            c.drawLine(x, top1, x, bot2, grid)
            val s = if (ts < 1) "%.1f".format(t) else "%.0f".format(t)
            c.drawText(s, x - axis.measureText(s) / 2, height - d(6), axis)
            t += ts
        }
        c.drawText("s", right - d(8), height - d(6), axis)

        c.save()
        c.clipRect(left, 0f, right, height.toFloat())

        // kalkış işaretleri
        for (l in r.launches) {
            val x = xOf(l - t0)
            if (x in left..right) c.drawLine(x, top1, x, bot2, launch)
        }

        // en iyi eğri (dolgu + parıltı + çizgi)
        if (buildPath(r.best, t0) { yV(it) }) {
            area.set(path)
            area.lineTo(lastX, bot1)
            area.lineTo(firstX, bot1)
            area.close()
            c.drawPath(area, fill)
            c.drawPath(path, glow)
            c.drawPath(path, line)
        }
        // OBD ve GPS noktaları
        dots(c, r.obd, t0, obdDot, d(1.7f))
        dots(c, r.gps, t0, gpsDot, d(2.3f))
        // g şeridi
        if (buildPath(r.gLong, t0) { yG(it) }) c.drawPath(path, gLine)
        c.restore()

        // imleç / ipucu
        val cx = cursor
        if (cx != null) {
            val x = xOf(cx)
            if (x in left..right) {
                c.drawLine(x, top1, x, bot2, cur)
                val sp = r.best.at(cx + t0)
                val g = r.gLong.at(cx + t0)
                if (!sp.isNaN()) c.drawCircle(x, yV(sp), d(4), readout)
                if (!g.isNaN()) c.drawCircle(x, yG(g), d(3), readout)
                val txt = buildString {
                    append("%.2f s".format(cx))
                    if (!sp.isNaN()) append("   %.1f %s".format(sp * k, unit))
                    if (!g.isNaN()) append("   %+.2f g".format(g))
                }
                val tw = readout.measureText(txt) + d(16)
                val bx = (x - tw / 2).coerceIn(left, right - tw)
                rect.set(bx, d(3), bx + tw, d(25))
                c.drawRoundRect(rect, d(8), d(8), readoutBg)
                c.drawText(txt, bx + d(8), d(19), readout)
            }
        } else {
            c.drawText("Dokun: değerler · 2 parmak: yakınlaştır", left, d(18), hint)
        }
    }

    private var firstX = 0f
    private var lastX = 0f

    /** Görünür aralığı Path'e dök; piksel sütunu başına min/maks. Nokta yoksa false. */
    private inline fun buildPath(s: RollAnalysis.Series, t0: Double, y: (Double) -> Float): Boolean {
        path.reset()
        if (s.size < 2) return false
        val i0 = max(lb(s.t, x0 + t0) - 1, 0)
        val i1 = min(lb(s.t, x1 + t0) + 1, s.size - 1)
        if (i1 <= i0) return false
        var col = Int.MIN_VALUE
        var lo = 0f
        var hi = 0f
        var started = false
        for (i in i0..i1) {
            val x = xOf(s.t[i] - t0)
            val yy = y(s.v[i])
            val cc = x.toInt()
            if (!started) { path.moveTo(x, yy); firstX = x; started = true; col = cc; lo = yy; hi = yy; continue }
            if (cc == col) { lo = min(lo, yy); hi = max(hi, yy); continue }
            if (hi - lo > 1f) { path.lineTo(col.toFloat(), lo); path.lineTo(col.toFloat(), hi) }
            path.lineTo(x, yy)
            col = cc; lo = yy; hi = yy
            lastX = x
        }
        return started
    }

    private fun dots(c: Canvas, s: RollAnalysis.Series, t0: Double, p: Paint, rad: Float) {
        if (s.size == 0) return
        val i0 = lb(s.t, x0 + t0)
        val i1 = lb(s.t, x1 + t0)
        val step = max(1, (i1 - i0) / max(1, (right - left).toInt()))   // piksel başına en çok ~1 nokta
        var i = i0
        while (i < i1) {
            c.drawCircle(xOf(s.t[i] - t0), yV(s.v[i]), rad, p)
            i += step
        }
    }

    // --- dokunma -------------------------------------------------------------------

    private val slop = ViewConfiguration.get(ctx).scaledTouchSlop
    private var downX = 0f
    private var downY = 0f
    private var decided = false
    private var pinch = false
    private var span0 = 0f
    private var mid0 = 0.0
    private var w0 = 0.0
    private var lastTap = 0L
    private var lastTapX = 0f

    private fun spanOf(e: MotionEvent) = hypot(e.getX(0) - e.getX(1), e.getY(0) - e.getY(1))
    private fun midX(e: MotionEvent) = (e.getX(0) + e.getX(1)) / 2

    private fun setCursor(x: Float) {
        cursor = tOf(x.coerceIn(left, right))
        invalidate()
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                parent?.requestDisallowInterceptTouchEvent(true)
                downX = e.x; downY = e.y
                decided = false; pinch = false
            }
            MotionEvent.ACTION_POINTER_DOWN -> if (e.pointerCount == 2) {
                decided = true; pinch = true
                span0 = max(spanOf(e), 1f)
                mid0 = tOf(midX(e))
                w0 = x1 - x0
            }
            MotionEvent.ACTION_MOVE -> when {
                pinch && e.pointerCount >= 2 -> {
                    val w = (w0 * span0 / max(spanOf(e), 1f)).coerceIn(min(2.0, full1 - full0), full1 - full0)
                    val frac = ((midX(e) - left) / (right - left)).toDouble()
                    var a = mid0 - frac * w
                    a = a.coerceIn(full0, full1 - w)
                    x0 = a; x1 = a + w
                    invalidate()
                }
                pinch -> Unit
                !decided -> {
                    val dx = abs(e.x - downX)
                    val dy = abs(e.y - downY)
                    if (dx > slop || dy > slop) {
                        if (dy > dx) {   // dikey: sayfa kaydırması
                            parent?.requestDisallowInterceptTouchEvent(false)
                            return false
                        }
                        decided = true
                        setCursor(e.x)
                    }
                }
                else -> setCursor(e.x)
            }
            MotionEvent.ACTION_UP -> {
                if (!decided) {
                    val now = e.eventTime
                    if (now - lastTap < 300 && abs(e.x - lastTapX) < slop * 3) {   // çift dokunuş: tümü
                        x0 = full0; x1 = full1
                        cursor = null
                        invalidate()
                        lastTap = 0
                    } else {
                        setCursor(e.x)
                        lastTap = now; lastTapX = e.x
                    }
                }
                parent?.requestDisallowInterceptTouchEvent(false)
            }
            MotionEvent.ACTION_CANCEL -> parent?.requestDisallowInterceptTouchEvent(false)
        }
        return true
    }
}
