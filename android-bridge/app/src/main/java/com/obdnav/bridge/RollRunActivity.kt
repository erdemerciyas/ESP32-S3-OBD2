package com.obdnav.bridge

import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.SpannableString
import android.text.Spanned
import android.text.style.RelativeSizeSpan
import android.view.Gravity
import android.view.View
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import com.obdnav.bridge.RollAnalysis.Kind
import com.obdnav.bridge.RollAnalysis.Measure
import com.obdnav.bridge.RollAnalysis.Result
import com.obdnav.bridge.RollAnalysis.Status
import com.obdnav.bridge.RollAnalysis.Target
import java.io.File
import java.text.SimpleDateFormat
import java.util.Locale
import kotlin.math.abs
import kotlin.math.roundToInt
import kotlin.math.roundToLong

/** Biçimlendirme + ayar köprüsü (liste ve görüntüleyici ortak). Süreler hep saniye, 2 ondalık. */
object RollFmt {
    fun params(c: RollCfg) = RollAnalysis.Params(
        c.spd.map { it.from to it.to }, c.dst.toList(), c.brk.toList(),
        c.ro, c.slope, c.slc, c.gacc, c.sats, c.obdk, c.src,
    )

    fun unit(mph: Boolean) = if (mph) "mph" else "km/h"
    private fun spd(kmh: Int, mph: Boolean) = if (mph) (kmh / 1.609344).roundToInt() else kmh

    fun label(t: Target, mph: Boolean) = when (t.kind) {
        Kind.SPEED -> "${spd(t.from, mph)}-${spd(t.to, mph)}"
        Kind.DISTANCE -> dist(t.distM)
        Kind.BRAKE -> "${spd(t.from, mph)}-0"
    }

    /** 18.288 → "60 ft", 402.336 → "402 m". */
    fun dist(m: Double): String {
        val ft = m / 0.3048
        return if (m < 100 && abs(ft - ft.roundToInt()) < 0.01) "${ft.roundToInt()} ft" else "${m.roundToInt()} m"
    }

    fun sec(x: Double?) = x?.let { "%.2f s".format(it) } ?: "—"
    fun speed(kmh: Double, mph: Boolean) = "%.0f %s".format(if (mph) kmh / 1.609344 else kmh, unit(mph))

    fun duration(s: Double): String {
        val n = s.roundToLong()
        return if (n >= 60) "${n / 60} dk ${n % 60} sn" else "$n sn"
    }

    fun status(s: Status): Pair<String, Int> = when (s) {
        Status.OK -> "Doğrulandı" to Ui.OK
        Status.SLOPE -> "Eğim aşıldı" to Ui.WARN
        Status.GPS_WEAK -> "GPS zayıf" to Ui.WARN
        Status.UNKNOWN -> "Eğim ölçülemedi" to Ui.WARN
    }

    /** Liste satırı özeti: "2 koşu · en iyi 0-100 15,2 s". */
    fun summary(r: Result, mph: Boolean): String {
        if (r.runs.isEmpty()) return if (r.launches.isEmpty()) "Kalkış algılanmadı" else "Hedeflere ulaşılmadı"
        val b = r.targets.firstNotNullOfOrNull { r.bestOf(it) }
        return "${r.runs.size} koşu" + (b?.let { " · en iyi ${label(it.target, mph)} ${"%.1f s".format(it.time)}" } ?: "")
    }

    /** roll_20261007_101500.csv → "7 Ekim 2026, 10:15:00". */
    fun title(f: File): String = try {
        val d = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).parse(f.name.removePrefix("roll_").removeSuffix(".csv"))!!
        SimpleDateFormat("d MMMM yyyy, HH:mm:ss", Locale("tr")).format(d)
    } catch (_: Exception) { f.name }

    /** Liste özetleri (anahtar: RollAnalysis.key). */
    val summaries = HashMap<String, String>()

    /** Koşu × hedef başına bir satır; ondalık nokta, boş = yok. */
    fun resultsCsv(f: File, r: Result): String = buildString {
        append("file,run_no,start_s,target,type,time_s,time_gps_s,time_obd_s,trap_kmh,dist_m,")
        append("slope_gps_pct,slope_imu_pct,time_slope_corr_s,valid,source\n")
        fun n(x: Double?, nd: Int) = x?.takeIf { !it.isNaN() }?.let { String.format(Locale.US, "%.${nd}f", it) } ?: ""
        for (run in r.runs) for (m in run.items) {
            val type = when (m.target.kind) { Kind.SPEED -> "speed"; Kind.DISTANCE -> "distance"; Kind.BRAKE -> "brake" }
            append(listOf(
                f.name, run.no.toString(), n(m.t1 - r.t0, 2), m.target.key, type,
                n(m.time, 3), n(m.timeGps, 3), n(m.timeObd, 3), n(m.trapKmh, 1), n(m.distM, 1),
                n(m.slopeGps, 2), n(m.slopeImu, 2), n(m.timeCorr, 3), if (m.valid) "1" else "0", r.source,
            ).joinToString(",")).append('\n')
        }
    }
}

/** Bir ROLL kaydının sonuçları: en iyi değerler, hız grafiği, koşular, kaynak kalitesi, dışa aktarım. */
class RollRunActivity : Activity() {
    private val main = Handler(Looper.getMainLooper())
    private lateinit var col: LinearLayout
    private lateinit var file: File
    private var res: Result? = null
    private val mph get() = RollSettings.cfg.unit == "mph"

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Ui.applyWindow(this)
        RollSettings.init(this)
        file = intent.getStringExtra(EXTRA_FILE)?.let { File(it) }?.takeIf { it.isFile } ?: run {
            Toast.makeText(this, "Kayıt bulunamadı", Toast.LENGTH_SHORT).show()
            finish()
            return
        }
        val root = Ui.column(this)
        root.addView(Ui.topBar(this, "ROLL sonuçları"))
        col = Ui.column(this, 16)
        root.addView(ScrollView(this).apply { addView(col) })
        setContentView(root)
        load()
    }

    private fun load() {
        val p = RollFmt.params(RollSettings.cfg)
        RollAnalysis.cached(file, p)?.let { show(it); return }
        col.removeAllViews()
        col.addView(header(null), Ui.lp(this, bottomDp = 12))
        col.addView(Ui.card(this).apply {
            gravity = Gravity.CENTER_HORIZONTAL
            addView(ProgressBar(context).apply { indeterminateTintList = android.content.res.ColorStateList.valueOf(Ui.PRIMARY) })
            addView(Ui.text(context, "Analiz ediliyor…", 15f, Ui.DIM).apply { setPadding(0, Ui.dp(context, 8), 0, 0) })
        })
        val f = file
        val work = {
            Thread {
                val r = try { RollAnalysis.analyzeCached(f, p) } catch (e: Throwable) { e }
                main.post {
                    if (isDestroyed) return@post
                    if (r is Result) show(r) else fail((r as Throwable).javaClass.simpleName)
                }
            }.apply { name = "roll-analysis" }.start()
        }
        if (f == Roll.file) Roll.afterFlush(work) else work()   // süren kayıt: önce diske bas
    }

    private fun fail(why: String) {
        col.removeAllViews()
        col.addView(header(null), Ui.lp(this, bottomDp = 12))
        col.addView(message("Kayıt çözümlenemedi", "Dosya okunamadı ya da bozuk ($why). Ham CSV'yi yine de dışa aktarabilirsin."),
            Ui.lp(this, bottomDp = 12))
        col.addView(actions(null))
    }

    private fun show(r: Result) {
        res = r
        RollFmt.summaries[RollAnalysis.key(file, RollFmt.params(RollSettings.cfg))] = RollFmt.summary(r, mph)
        col.removeAllViews()
        col.addView(header(r), Ui.lp(this, bottomDp = 6))
        when {
            r.runs.isNotEmpty() -> {
                col.addView(Ui.section(this, "En iyi sonuçlar"))
                col.addView(hero(r), Ui.lp(this, bottomDp = 6))
            }
            r.best.size < 2 -> col.addView(message("Hız verisi yok",
                "Bu kayıtta GPS fix'i ya da OBD hızı bulunmuyor. Köprü açıkken ROLL modunda yeniden ölçüm yap."), Ui.lp(this, bottomDp = 6))
            r.launches.isEmpty() -> col.addView(message("Duruştan kalkış algılanmadı",
                "Araç tamamen dururken gaza basıldığında koşu başlar. Ara hız hedefleri (ör. 60-120) için hız eşiğinin geçilmesi gerekir."),
                Ui.lp(this, bottomDp = 6))
            else -> col.addView(message("Hedeflere ulaşılmadı",
                "Kalkış algılandı ama ayarlardaki hız / mesafe hedeflerinin hiçbiri tamamlanmadı."), Ui.lp(this, bottomDp = 6))
        }
        if (r.duration > 0) {
            col.addView(Ui.section(this, "Hız grafiği"))
            col.addView(chart(r), Ui.lp(this, bottomDp = 6))
        }
        if (r.runs.isNotEmpty()) {
            col.addView(Ui.section(this, if (r.runs.size == 1) "Koşu" else "Koşular · ${r.runs.size}"))
            r.runs.forEach { col.addView(runCard(r, it), Ui.lp(this, bottomDp = 10)) }
        }
        col.addView(Ui.section(this, "Kaynak kalitesi"))
        col.addView(quality(r), Ui.lp(this, bottomDp = 6))
        col.addView(Ui.section(this, "Dışa aktar"))
        col.addView(actions(r))
    }

    // --- parçalar -------------------------------------------------------------------

    private fun pill(s: String, color: Int) = Ui.text(this, s, 12f, color, true).apply {
        background = Ui.rounded(context, (color and 0x00FFFFFF) or 0x22000000, 10, color)
        setPadding(Ui.dp(context, 10), Ui.dp(context, 3), Ui.dp(context, 10), Ui.dp(context, 4))
    }

    private fun header(r: Result?): View = Ui.card(this).apply {
        addView(Ui.text(context, RollFmt.title(file), 19f, Ui.TEXT, true))
        val parts = mutableListOf<String>()
        r?.let { parts += RollFmt.duration(it.duration) }
        parts += RollSettings.cfg.name
        parts += Roll.sizeText(file.length())
        addView(Ui.text(context, parts.joinToString(" · "), 13f, Ui.DIM).apply { setPadding(0, Ui.dp(context, 2), 0, 0) })
        if (r == null) return@apply
        val badges = Ui.FlowLayout(context, 6).apply { setPadding(0, Ui.dp(context, 10), 0, 0) }
        if (r.source != "—") badges.addView(pill(r.source, Ui.PRIMARY))
        val ms = r.measures
        if (ms.isNotEmpty()) {
            val ok = ms.count { it.valid }
            val (txt, c) = when (ok) {
                ms.size -> "Doğrulandı" to Ui.OK
                0 -> RollFmt.status(ms.maxBy { RollAnalysis.rank(it.status) }.status)
                else -> "Kısmen doğrulandı · $ok/${ms.size}" to Ui.WARN
            }
            badges.addView(pill(txt, c))
        }
        if (file == Roll.file) badges.addView(pill("Kayıt sürüyor", Ui.ACCENT))
        if (badges.childCount > 0) addView(badges)
    }

    private fun message(title: String, sub: String): View = Ui.card(this).apply {
        addView(Ui.text(context, title, 17f, Ui.TEXT, true))
        addView(Ui.text(context, sub, 14f, Ui.DIM).apply { setPadding(0, Ui.dp(context, 4), 0, 0) })
    }

    /** "15,22 s" — sayı büyük, birim küçük. */
    private fun bigTime(x: Double, sp: Float, color: Int): TextView {
        val s = SpannableString("%.2f s".format(x))
        s.setSpan(RelativeSizeSpan(0.5f), s.length - 2, s.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        return Ui.text(this, s, sp, color, true).apply { includeFontPadding = false }
    }

    private fun hero(r: Result): View {
        val runOf = HashMap<Measure, Int>()
        r.runs.forEach { run -> run.items.forEach { runOf[it] = run.no } }
        val bests = r.targets.mapNotNull { r.bestOf(it) }
        val box = Ui.column(this)
        fun tile(m: Measure, big: Boolean): View = Ui.column(this, if (big) 18 else 14).apply {
            background = Ui.rounded(context, Ui.SURFACE, 18, if (big) 0x6600E5FF else Ui.BORDER)
            addView(Ui.row(context).apply {
                addView(Ui.text(context, RollFmt.label(m.target, mph) + if (m.target.kind == Kind.SPEED || m.target.kind == Kind.BRAKE) " ${RollFmt.unit(mph)}" else "",
                    if (big) 14f else 12.5f, Ui.DIM, true), Ui.lp(context, 0, weight = 1f))
                addView(Ui.dot(context, RollFmt.status(m.status).second, 8).apply {
                    (layoutParams as LinearLayout.LayoutParams).marginEnd = 0
                })
            })
            addView(bigTime(m.time, if (big) 44f else 28f, if (big) Ui.PRIMARY else Ui.TEXT).apply {
                setPadding(0, Ui.dp(context, if (big) 6 else 4), 0, Ui.dp(context, if (big) 6 else 4))
            })
            val sub = mutableListOf<String>()
            when (m.target.kind) {
                Kind.DISTANCE -> m.trapKmh?.let { sub += "@ ${RollFmt.speed(it, mph)}" }
                Kind.BRAKE -> m.distM?.let { sub += "%.1f m".format(it) }
                Kind.SPEED -> {}
            }
            runOf[m]?.let { if (r.runs.size > 1) sub += "Koşu $it" }
            if (m.timeCorr != null) sub += "düz yol ${RollFmt.sec(m.timeCorr)}"
            if (!m.valid) sub += RollFmt.status(m.status).first
            if (sub.isNotEmpty()) addView(Ui.text(context, sub.joinToString(" · "), 12f, if (m.valid) Ui.DIM else Ui.WARN))
        }
        bests.firstOrNull()?.let { box.addView(tile(it, true), Ui.lp(this, bottomDp = 10)) }
        bests.drop(1).chunked(2).forEach { pair ->
            box.addView(Ui.row(this).apply {
                gravity = Gravity.NO_GRAVITY
                pair.forEachIndexed { i, m ->
                    addView(tile(m, false), Ui.lp(context, 0, weight = 1f).apply { if (i == 0) marginEnd = Ui.dp(context, 5) else marginStart = Ui.dp(context, 5) })
                }
                if (pair.size == 1) addView(View(context), Ui.lp(context, 0, weight = 1f).apply { marginStart = Ui.dp(context, 5) })
            }, Ui.lp(this, bottomDp = 10))
        }
        val miss = r.missing
        if (miss.isNotEmpty())
            box.addView(Ui.text(this, "Ulaşılmayan: " + miss.joinToString(", ") { RollFmt.label(it, mph) }, 12.5f, Ui.DIM)
                .apply { setPadding(Ui.dp(context, 4), 0, 0, Ui.dp(context, 4)) })
        return box
    }

    private fun chart(r: Result): View = Ui.card(this).apply {
        setPadding(Ui.dp(context, 8), Ui.dp(context, 12), Ui.dp(context, 8), Ui.dp(context, 12))
        addView(RollChart(context, r, mph), Ui.lp(context))
        val legend = Ui.FlowLayout(context, 12).apply { setPadding(Ui.dp(context, 8), Ui.dp(context, 10), Ui.dp(context, 8), 0) }
        fun item(color: Int, s: String) = Ui.row(context).apply {
            addView(Ui.dot(context, color, 8).apply { (layoutParams as LinearLayout.LayoutParams).marginEnd = Ui.dp(context, 6) })
            addView(Ui.text(context, s, 12f, Ui.DIM))
        }
        if (r.best.size > 1) legend.addView(item(Ui.PRIMARY, "Hız (${r.source})"))
        if (r.gps.size > 0) legend.addView(item(Ui.TEXT, "GPS"))
        if (r.obd.size > 0) legend.addView(item(Ui.ACCENT, "OBD"))
        if (r.launches.isNotEmpty()) legend.addView(item(Ui.OK, "Kalkış"))
        if (r.gLong.size > 1) legend.addView(item(Ui.WARN, "Boyuna g"))
        addView(legend)
        addView(Ui.text(context, "Çift dokun: tüm kayıt · numaralı bantlar koşulardır", 11.5f, Ui.DIM)
            .apply { setPadding(Ui.dp(context, 8), Ui.dp(context, 6), Ui.dp(context, 8), 0) })
    }

    private fun runCard(r: Result, run: RollAnalysis.Run): View = Ui.card(this).apply {
        addView(Ui.row(context).apply {
            addView(Ui.text(context, "Koşu ${run.no}", 17f, Ui.TEXT, true), Ui.lp(context, 0, weight = 1f))
            val (txt, c) = RollFmt.status(run.status)
            addView(pill(txt, c))
        })
        val info = mutableListOf("${if (run.launch) "Kalkış" else "Başlangıç"} %.1f s".format(run.start - r.t0))
        info += "maks ${RollFmt.speed(run.maxKmh, mph)}"
        if (run.peakG > 0.01) info += "%.2f g".format(run.peakG)
        if (run.minG < -0.05) info += "fren %.2f g".format(-run.minG)
        addView(Ui.text(context, info.joinToString(" · "), 13f, Ui.DIM).apply { setPadding(0, Ui.dp(context, 2), 0, Ui.dp(context, 4)) })
        run.items.forEach { m ->
            addView(Ui.divider(context))
            addView(Ui.row(context).apply {
                gravity = Gravity.TOP
                setPadding(0, Ui.dp(context, 10), 0, 0)
                addView(Ui.column(context).apply {
                    addView(Ui.text(context, RollFmt.label(m.target, mph) + when (m.target.kind) {
                        Kind.DISTANCE -> ""
                        else -> " ${RollFmt.unit(mph)}"
                    }, 15f, Ui.TEXT, true))
                    addView(Ui.text(context, details(m), 12.5f, Ui.DIM).apply { setPadding(0, Ui.dp(context, 3), Ui.dp(context, 8), 0) })
                }, Ui.lp(context, 0, weight = 1f))
                addView(bigTime(m.time, 22f, Ui.PRIMARY))
                addView(Ui.text(context, if (m.valid) "✓" else "!", 16f, RollFmt.status(m.status).second, true).apply {
                    gravity = Gravity.CENTER
                    setPadding(Ui.dp(context, 10), 0, 0, 0)
                })
            })
        }
    }

    private fun details(m: Measure): String {
        val p = mutableListOf<String>()
        when (m.target.kind) {
            Kind.DISTANCE -> m.trapKmh?.let { p += "@ ${RollFmt.speed(it, mph)}" }
            Kind.BRAKE -> m.distM?.let { p += "mesafe %.1f m".format(it) }
            Kind.SPEED -> {}
        }
        p += "GPS ${RollFmt.sec(m.timeGps)}"
        p += "OBD ${RollFmt.sec(m.timeObd)}"
        when {
            m.slopeGps != null && m.slopeImu != null -> p += "eğim %%%.1f (IMU %%%.1f)".format(m.slopeGps, m.slopeImu)
            m.slope != null -> p += "eğim %%%.1f".format(m.slope)
        }
        m.timeCorr?.let { p += "düz yol ${RollFmt.sec(it)}" }
        if (!m.valid) p += RollFmt.status(m.status).first
        return p.joinToString(" · ")
    }

    private fun quality(r: Result): View = Ui.card(this).apply {
        val q = r.quality
        val cfg = RollSettings.cfg
        fun row(color: Int, label: String, value: String) = addView(Ui.row(context).apply {
            gravity = Gravity.TOP
            setPadding(0, Ui.dp(context, 5), 0, Ui.dp(context, 5))
            addView(Ui.dot(context, color, 9).apply {
                (layoutParams as LinearLayout.LayoutParams).topMargin = Ui.dp(context, 5)
            })
            addView(Ui.text(context, label, 13.5f, Ui.DIM), Ui.lp(context, 0, weight = 1f))
            addView(Ui.text(context, value, 13.5f, Ui.TEXT).apply { gravity = Gravity.END }, Ui.lp(context, 0, weight = 1.4f))
        })
        fun grade(ok: Boolean, warn: Boolean) = if (ok) Ui.OK else if (warn) Ui.WARN else Ui.CRIT

        if (q.gpsFixes == 0) row(Ui.CRIT, "GPS", "yok")
        else {
            row(grade(q.gpsHz >= 4.5, q.gpsHz >= 0.9), "GPS", "${q.gpsFixes} fix · %.1f Hz".format(q.gpsHz))
            q.saMedKmh?.let { row(grade(it <= cfg.gacc * 3.6, it <= cfg.gacc * 7.2), "GPS hız doğruluğu", "medyan %.2f km/h".format(it)) }
            if (q.satsMed != null && q.satsMin != null)
                row(grade(q.satsMed >= cfg.sats && q.satsMin >= cfg.sats, q.satsMed >= cfg.sats), "Uydu",
                    "medyan %.0f · en az %d".format(q.satsMed, q.satsMin))
            row(grade(q.gpsGaps == 0, q.gpsGaps <= 3), "GPS boşluğu (> 1,5 s)", "${q.gpsGaps}")
        }
        if (q.imuN == 0) row(Ui.CRIT, "IMU", "yok")
        else row(grade(q.imuHz >= 90, q.imuHz >= 50), "IMU",
            "${q.imuN} örnek · %.0f Hz".format(q.imuHz) + if (q.imuGaps > 0) " · ${q.imuGaps} boşluk" else "")
        if (q.obdN == 0) row(Ui.DIM, "OBD", "yok")
        else row(grade(q.obdHz >= 4, q.obdHz >= 1.5), "OBD",
            "%.1f Hz".format(q.obdHz) + (q.obdLatMs?.let { " · yanıt %.0f ms".format(it) } ?: ""))
        if (q.syncN == 0 || q.rttBestMs == null) row(Ui.WARN, "Saat senkronu", "yok")
        else row(grade(q.rttBestMs <= 60, q.rttBestMs <= 150), "Saat senkronu",
            "RTT medyan %.0f ms · en iyi %.0f ms".format(q.rttMedMs ?: 0.0, q.rttBestMs))
        val c = q.calib
        when {
            c == null -> if (q.obdN > 0) row(Ui.DIM, "OBD ölçeği", "hesaplanamadı")
            c.manual && c.n == 0 -> row(Ui.PRIMARY, "OBD ölçeği", "k %.3f (elle)".format(c.k))
            else -> row(grade(abs(c.k - 1) <= 0.05 && c.rms <= 2, true), "OBD ölçeği",
                "k %.4f%s · gecikme %.0f ms · RMS %.2f km/h".format(c.k,
                    if (c.manual) " (elle)" else " (%+.1f%%)".format((c.k - 1) * 100), c.tauS * 1000, c.rms))
        }
        row(Ui.PRIMARY, "Kayıt süresi", RollFmt.duration(r.duration))
        if (r.maxKmh > 0) row(Ui.PRIMARY, "Maks hız", RollFmt.speed(r.maxKmh, mph))
        if (r.peakG > 0 || r.minG < 0) row(Ui.PRIMARY, "Tepe ivme / fren", "%.2f g · %.2f g".format(r.peakG, -r.minG))
    }

    private fun actions(r: Result?): View = Ui.card(this).apply {
        if (r != null) addView(Ui.button(context, "Sonuçları CSV olarak dışa aktar", Ui.Style.PRIMARY) { exportResults() },
            Ui.lp(context, bottomDp = 10))
        addView(Ui.row(context).apply {
            addView(Ui.button(context, "Ham CSV dışa aktar", Ui.Style.SECONDARY) { RollLogActivity.export(this@RollRunActivity, file) },
                Ui.lp(context, 0, weight = 1f).apply { marginEnd = Ui.dp(context, 5) })
            addView(Ui.button(context, "Paylaş", Ui.Style.SECONDARY) { shareMenu() },
                Ui.lp(context, 0, weight = 1f).apply { marginStart = Ui.dp(context, 5) })
        })
        addView(Ui.text(context, "Dışa aktarılan dosyalar: İndirilenler/AURA", 12f, Ui.DIM).apply {
            gravity = Gravity.CENTER
            setPadding(0, Ui.dp(context, 10), 0, 0)
        })
    }

    // --- dışa aktarım ------------------------------------------------------------------

    /** Sonuç CSV'sini arka planda yazar, sonra ana iş parçacığında devam eder. */
    private fun withResults(then: (File) -> Unit) {
        val r = res ?: return
        if (r.measures.isEmpty()) {
            Toast.makeText(this, "Dışa aktarılacak koşu yok", Toast.LENGTH_SHORT).show()
            return
        }
        val out = RollFileProvider.resultsFile(this, file)
        val raw = file
        Thread {
            val ok = try { out.writeText(RollFmt.resultsCsv(raw, r)); true } catch (_: Exception) { false }
            main.post {
                if (isDestroyed) return@post
                if (ok) then(out) else Toast.makeText(this, "Sonuçlar yazılamadı", Toast.LENGTH_SHORT).show()
            }
        }.start()
    }

    private fun exportResults() = withResults { RollLogActivity.export(this, it) }

    private fun shareMenu() {
        val hasRuns = res?.measures?.isNotEmpty() == true
        if (!hasRuns) { RollLogActivity.share(this, listOf(file), "ROLL kaydını paylaş"); return }
        AlertDialog.Builder(this)
            .setTitle("Paylaş")
            .setItems(arrayOf("Sonuçlar (CSV)", "Ham kayıt (CSV)", "İkisi birden")) { _, which ->
                when (which) {
                    0 -> withResults { RollLogActivity.share(this, listOf(it), "ROLL sonuçlarını paylaş") }
                    1 -> RollLogActivity.share(this, listOf(file), "ROLL kaydını paylaş")
                    else -> withResults { RollLogActivity.share(this, listOf(it, file), "ROLL kaydını paylaş") }
                }
            }
            .show()
    }

    companion object {
        const val EXTRA_FILE = "file"

        fun open(ctx: Context, f: File) {
            ctx.startActivity(android.content.Intent(ctx, RollRunActivity::class.java).putExtra(EXTRA_FILE, f.path))
        }
    }
}
