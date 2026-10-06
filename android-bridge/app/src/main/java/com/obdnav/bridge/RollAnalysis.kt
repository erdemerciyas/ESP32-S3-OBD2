package com.obdnav.bridge

import java.io.BufferedReader
import java.io.File
import java.io.FileInputStream
import java.io.InputStreamReader
import java.math.BigDecimal
import java.math.RoundingMode
import kotlin.math.abs
import kotlin.math.atan
import kotlin.math.ceil
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin
import kotlin.math.sqrt
import kotlin.math.tan

/**
 * ROLL kayıt analizi — scripts/roll_analyze.py'nin Kotlin karşılığı.
 * Android bağımlılığı yok (JVM'de de derlenip doğrulanabilir).
 *
 * Zaman tabanı ESP µs → s. Hız eğrileri: GPS (Doppler), OBD (istek/yanıt
 * ortası) ve FÜZYON (IMU ivmesi GPS / OBD fix'lerine aralık başına doğrusal
 * bias düzeltmesiyle bağlanır). Koşular, kullanıcının hedefleri (hız aralığı,
 * mesafe, frenleme) için süre, eğim ve doğrulama ile çıkarılır.
 */
object RollAnalysis {
    const val G0 = 9.80665
    private const val LAUNCH_G = 0.08     // kalkış: boyuna ivme eşiği (g), 100 ms sürmeli
    private const val FLOOR_G = 0.02      // kalkış anı bu gürültü tabanına geri çekilir
    private const val ROLLOUT_M = 0.3048  // 1 ft
    private const val STOP_KMH = 3.0      // duruş eşiği (kalkış / koşu bölütleme)
    private const val DRAW_MAX = 40_000   // çizim için en çok nokta

    data class Params(
        val spd: List<Pair<Int, Int>>,
        val dst: List<Double>,
        val brk: List<Int>,
        val rollout: Boolean = false,
        val slopeLimit: Double = 1.0,     // %, 0 = kapalı
        val slc: Boolean = true,
        val gacc: Double = 1.0,           // m/s
        val sats: Int = 6,
        val obdk: Double = 0.0,           // 0 = otomatik
        val src: String = "auto",
    )

    enum class Kind { SPEED, DISTANCE, BRAKE }
    enum class Status { OK, SLOPE, GPS_WEAK, UNKNOWN }

    data class Target(val kind: Kind, val from: Int = 0, val to: Int = 0, val distM: Double = 0.0) {
        /** Makine etiketi (CSV): 0-100, 402.336m, 100-0 — hız her zaman km/h. */
        val key: String get() = when (kind) {
            Kind.SPEED -> "$from-$to"
            Kind.DISTANCE -> "${meters(distM)}m"
            Kind.BRAKE -> "$from-0"
        }
    }

    class Series(val t: DoubleArray, val v: DoubleArray) {
        val size get() = t.size
        fun isEmpty() = t.size < 2
        /** Doğrusal ara değer; aralık dışında NaN. */
        fun at(x: Double) = interp(t, v, x)

        companion object { val EMPTY = Series(DoubleArray(0), DoubleArray(0)) }
    }

    class Calib(val k: Double, val tauS: Double, val rms: Double, val n: Int, val manual: Boolean)

    class Measure(
        val target: Target,
        val t1: Double, val t2: Double,
        val time: Double,                 // en iyi eğri (s)
        val timeGps: Double?, val timeObd: Double?, val timeObdK: Double?,
        val trapKmh: Double?,             // mesafe: d anındaki hız
        val distM: Double?,               // koşu boyunca katedilen mesafe (fren: duruş mesafesi)
        val slopeGps: Double?, val slopeImu: Double?,
        val timeCorr: Double?,            // eğim düzeltmeli süre (slc)
        val status: Status,
    ) {
        val valid get() = status == Status.OK
        val slope get() = slopeGps ?: slopeImu
    }

    class Run(
        val no: Int, val start: Double, val end: Double, val launch: Boolean,
        val maxKmh: Double, val peakG: Double, val minG: Double,
        val items: List<Measure>,
    ) {
        val status: Status get() = items.map { it.status }.maxByOrNull { rank(it) } ?: Status.OK
    }

    class Quality(
        val gpsFixes: Int, val gpsHz: Double, val gpsGaps: Int,
        val saMedKmh: Double?, val satsMed: Double?, val satsMin: Int?,
        val imuN: Int, val imuHz: Double, val imuGaps: Int,
        val obdN: Int, val obdHz: Double, val obdGaps: Int, val obdLatMs: Double?,
        val syncN: Int, val rttMedMs: Double?, val rttBestMs: Double?,
        val calib: Calib?,
    )

    class Result(
        val t0: Double, val duration: Double,
        val source: String, val gpsBased: Boolean,
        val best: Series,                 // çizim için seyreltilmiş en iyi eğri (km/h)
        val gps: Series, val obd: Series, // ham noktalar (km/h)
        val gLong: Series,                // boyuna g (~0.1 s ortalama, seyreltilmiş)
        val launches: List<Double>,
        val runs: List<Run>,
        val targets: List<Target>,
        val quality: Quality,
        val maxKmh: Double, val peakG: Double, val minG: Double,
        val events: List<String>, val header: String?,
    ) {
        val measures get() = runs.flatMap { it.items }
        /** Hedefin en iyi (en kısa) ölçümü. */
        fun bestOf(t: Target): Measure? = measures.filter { it.target == t }.minByOrNull { it.time }
        val missing get() = targets.filter { bestOf(it) == null }
    }

    fun rank(s: Status) = when (s) { Status.OK -> 0; Status.UNKNOWN -> 1; Status.SLOPE -> 2; Status.GPS_WEAK -> 3 }

    /** En çok 3 ondalık, sondaki sıfırlar atılır (18.288, 100). */
    fun meters(x: Double): String = BigDecimal.valueOf(x).setScale(3, RoundingMode.HALF_UP).stripTrailingZeros().toPlainString()

    // --- bellek önbelleği (dosya adı + boyut + değişim zamanı + ayarlar) -------

    private val cache = object : LinkedHashMap<String, Result>(4, 0.75f, true) {
        override fun removeEldestEntry(eldest: MutableMap.MutableEntry<String, Result>?) = size > 2
    }

    fun key(f: File, p: Params) = "${f.name}|${f.length()}|${f.lastModified()}|$p"

    fun cached(f: File, p: Params): Result? = synchronized(cache) { cache[key(f, p)] }

    /** Arka plan iş parçacığında çağrılmalı. */
    fun analyzeCached(f: File, p: Params): Result {
        val k = key(f, p)
        synchronized(cache) { cache[k] }?.let { return it }
        val r = analyze(f, p)
        synchronized(cache) { cache[k] = r }
        return r
    }

    // --- okuma -------------------------------------------------------------------

    private class DList(cap: Int = 256) {
        var a = DoubleArray(cap)
        var n = 0
        fun add(x: Double) {
            if (n == a.size) a = a.copyOf(n * 2)
            a[n++] = x
        }
        operator fun get(i: Int) = a[i]
        fun toArray(): DoubleArray = a.copyOf(n)
    }

    private object BadNum : RuntimeException() {
        override fun fillInStackTrace(): Throwable = this
    }

    private val POW10 = DoubleArray(19) { var p = 1.0; repeat(it) { p *= 10 }; p }

    /** s[a, b) → sayı; boş = NaN, bozuk = BadNum (Python'daki ValueError). */
    private fun num(s: String, a: Int, b: Int): Double {
        var i = a
        var e = b
        while (i < e && s[i] == ' ') i++
        while (e > i && s[e - 1] == ' ') e--
        if (i >= e) return Double.NaN
        var neg = false
        if (s[i] == '-' || s[i] == '+') { neg = s[i] == '-'; i++ }
        var mant = 0L
        var digits = 0
        var frac = 0
        var dot = false
        while (i < e) {
            val c = s[i]
            if (c in '0'..'9') {
                if (digits >= 18) return slow(s, a, b)
                mant = mant * 10 + (c - '0')
                digits++
                if (dot) frac++
            } else if (c == '.' && !dot) dot = true
            else return slow(s, a, b)
            i++
        }
        if (digits == 0) throw BadNum
        val v = if (frac > 0) mant / POW10[frac] else mant.toDouble()
        return if (neg) -v else v
    }

    private fun slow(s: String, a: Int, b: Int): Double = s.substring(a, b).trim().toDoubleOrNull() ?: throw BadNum

    private class Raw {
        val gEsp = DList(); val gPhone = DList(); val gV = DList(); val gSa = DList(); val gAlt = DList(); val gSat = DList()
        val iT = DList(4096); val iAx = DList(4096); val iPitch = DList(4096)
        val oTx = DList(); val oRx = DList(); val oKmh = DList()
        val sPhone = DList(); val sOff = DList(); val sRtt = DList()
        val events = ArrayList<String>()
        var header: String? = null
    }

    private fun load(f: File): Raw {
        val r = Raw()
        val comma = IntArray(16)
        BufferedReader(InputStreamReader(FileInputStream(f), Charsets.UTF_8), 1 shl 16).use { br ->
            while (true) {
                val line = (br.readLine() ?: break).trim()
                if (line.isEmpty()) continue
                if (line[0] == '#') {
                    if (r.header == null) r.header = line.trimStart('#', ' ')
                    continue
                }
                var nc = 0
                for (i in line.indices) if (line[i] == ',') { if (nc < comma.size) comma[nc] = i; nc++ }
                if (nc == 0 || comma[0] != 1) {
                    if (line == "M") r.events.add("")
                    continue
                }
                val nf = nc + 1
                fun fs(k: Int) = if (k == 0) 0 else comma[k - 1] + 1
                fun fe(k: Int) = if (k < min(nc, comma.size)) comma[k] else line.length
                fun n(k: Int) = num(line, fs(k), fe(k))
                fun req(k: Int): Double { val x = n(k); if (x.isNaN()) throw BadNum; return x }
                try {
                    when (line[0]) {
                        'G' -> if (nf >= 12) {
                            val esp = n(1); val phone = n(2); val v = n(3); val sa = n(4); val alt = n(5)
                            n(6); n(7); n(8)
                            val sat = n(9)
                            n(10); n(11)
                            r.gEsp.add(esp); r.gPhone.add(phone); r.gV.add(v); r.gSa.add(sa); r.gAlt.add(alt); r.gSat.add(sat)
                        }
                        'I' -> if (nf >= 7) {
                            val t = req(1); val ax = req(2) / 1000
                            req(3); req(4); req(5)
                            val p = req(6) / 100
                            r.iT.add(t); r.iAx.add(ax); r.iPitch.add(p)
                        }
                        'O' -> if (nf >= 4) {
                            val tx = req(1); val rx = req(2); val k = req(3)
                            r.oTx.add(tx); r.oRx.add(rx); r.oKmh.add(k)
                        }
                        'S' -> if (nf >= 4) {
                            val ph = req(1); val off = req(2); val rtt = req(3)
                            r.sPhone.add(ph); r.sOff.add(off); r.sRtt.add(rtt)
                        }
                        'M' -> if (r.events.size < 200) r.events.add(line.substring(2))
                    }
                } catch (_: BadNum) { }
            }
        }
        return r
    }

    // --- yardımcılar ---------------------------------------------------------------

    /** Python bisect_left. */
    private fun lb(ts: DoubleArray, x: Double, from: Int = 0, to: Int = ts.size): Int {
        var lo = from
        var hi = to
        while (lo < hi) {
            val m = (lo + hi) ushr 1
            if (ts[m] < x) lo = m + 1 else hi = m
        }
        return lo
    }

    /** Doğrusal ara değer; aralık dışında NaN. */
    fun interp(ts: DoubleArray, vs: DoubleArray, x: Double): Double {
        val i = lb(ts, x)
        if (i <= 0 || i >= ts.size) {
            if (i < ts.size && ts[i] == x) return vs[i]
            return Double.NaN
        }
        val t0 = ts[i - 1]
        val t1 = ts[i]
        if (t1 == t0) return vs[i]
        return vs[i - 1] + (vs[i] - vs[i - 1]) * (x - t0) / (t1 - t0)
    }

    /** [i0, i1) aralığında level'ı yukarı kesen ilk an; yoksa NaN. */
    private fun crossing(ts: DoubleArray, vs: DoubleArray, level: Double, i0: Int, i1: Int): Double {
        for (i in max(i0, 1) until i1) {
            if (vs[i - 1] < level && level <= vs[i])
                return ts[i - 1] + (level - vs[i - 1]) * (ts[i] - ts[i - 1]) / (vs[i] - vs[i - 1])
        }
        return Double.NaN
    }

    private fun median(xs: List<Double>): Double? {
        if (xs.isEmpty()) return null
        val s = xs.sorted()
        val m = s.size / 2
        return if (s.size % 2 == 1) s[m] else (s[m - 1] + s[m]) / 2
    }

    private fun rate(ts: DoubleArray) =
        if (ts.size > 1 && ts.last() > ts[0]) (ts.size - 1) / (ts.last() - ts[0]) else 0.0

    private fun gaps(ts: DoubleArray, lim: Double): Int {
        var c = 0
        for (i in 1 until ts.size) if (ts[i] - ts[i - 1] > lim) c++
        return c
    }

    /** Hız eğrisi + kümülatif mesafe (m, yamuk integral). */
    private class Curve(val t: DoubleArray, val v: DoubleArray) {
        val n = t.size
        val cum = DoubleArray(n).also { c ->
            for (i in 1 until n) c[i] = c[i - 1] + 0.5 * (v[i - 1] + v[i]) / 3.6 * (t[i] - t[i - 1])
        }
        val ok get() = n >= 2
        fun dAt(x: Double): Double = interp(t, cum, x.coerceIn(t[0], t[n - 1]))
        fun inRange(x: Double) = ok && x >= t[0] && x <= t[n - 1]

        companion object { val EMPTY = Curve(DoubleArray(0), DoubleArray(0)) }
    }

    // --- hız eğrileri ----------------------------------------------------------------

    private class Fused(val t: DoubleArray, val v: DoubleArray)

    /**
     * IMU ivmesini bağlantı noktalarına bağla: her aralıkta integral uyuşmazlığını
     * aralık boyunca doğrusal dağıt (sabit bias). > 3 s boşluklar köprülenmez.
     */
    private fun fuse(imuT: DoubleArray, imuA: DoubleArray, ancT: DoubleArray, ancV: DoubleArray): Fused {
        if (ancT.size < 2 || imuT.size < 2) return Fused(DoubleArray(0), DoubleArray(0))
        val outT = DList(imuT.size + ancT.size + 16)
        val outV = DList(imuT.size + ancT.size + 16)
        val segT = DList(1024)
        val segDv = DList(1024)
        var j = lb(imuT, ancT[0])
        for (k in 0 until ancT.size - 1) {
            val t0 = ancT[k]
            val t1 = ancT[k + 1]
            if (t1 - t0 > 3.0) {
                j = lb(imuT, t1)
                continue
            }
            segT.n = 0; segDv.n = 0
            segT.add(t0); segDv.add(0.0)
            var v = 0.0
            var tp = t0
            var ap = interp(imuT, imuA, t0).let { if (it.isNaN()) 0.0 else it }
            while (j < imuT.size && imuT[j] <= t1) {
                v += 0.5 * (ap + imuA[j]) * (imuT[j] - tp)
                tp = imuT[j]; ap = imuA[j]
                segT.add(tp); segDv.add(v)
                j++
            }
            val a1 = interp(imuT, imuA, t1).let { if (it.isNaN() || it == 0.0) ap else it }   // Python: "or ap"
            v += 0.5 * (ap + a1) * (t1 - tp)
            segT.add(t1); segDv.add(v)
            val err = (ancV[k + 1] - ancV[k]) / 3.6 - v
            for (i in 0 until segT.n - 1) {
                val frac = (segT[i] - t0) / (t1 - t0)
                outT.add(segT[i])
                outV.add(ancV[k] + (segDv[i] + err * frac) * 3.6)
            }
        }
        outT.add(ancT.last())
        outV.add(ancV.last())
        return Fused(outT.toArray(), outV.toArray())
    }

    /** OBD = k · GPS(t − tau): tau taraması −500..1500 ms, k en küçük kareler (v > 30 km/h, ≥ 20 çift). */
    private fun obdCalib(gt: DoubleArray, gv: DoubleArray, ot: DoubleArray, ov: DoubleArray, fixedK: Double?): Calib? {
        var best: Calib? = null
        val pv = DoubleArray(ov.size)
        val pg = DoubleArray(ov.size)
        for (tauMs in -500..1500 step 10) {
            val tau = tauMs / 1000.0
            var num = 0.0
            var den = 0.0
            var np = 0
            for (i in ot.indices) {
                val g = interp(gt, gv, ot[i] - tau)
                if (g.isNaN() || g < 30) continue
                pv[np] = ov[i]; pg[np] = g; np++
                num += ov[i] * g
                den += g * g
            }
            if (np < 20 || den == 0.0) continue
            val k = fixedK ?: (num / den)
            var ss = 0.0
            for (i in 0 until np) { val d = pv[i] - k * pg[i]; ss += d * d }
            val rms = sqrt(ss / np)
            if (best == null || rms < best.rms) best = Calib(k, tau, rms, np, fixedK != null)
        }
        return best
    }

    /** Durağan → kalkış anları (IMU). */
    private fun launches(t: DoubleArray, a: DoubleArray): List<Double> {
        val out = ArrayList<Double>()
        val n = t.size
        var i = 0
        while (i < n) {
            if (a[i] / G0 > LAUNCH_G) {
                var j = i
                while (j < n && a[j] / G0 > LAUNCH_G && t[j] - t[i] < 0.1) j++
                if (j < n && t[j] - t[i] >= 0.1) {
                    var k = i
                    while (k > 0 && a[k] / G0 > FLOOR_G && t[i] - t[k] < 1.0) k--
                    out.add(t[k])
                    while (i < n && a[i] / G0 > 0) i++   // bu hızlanma bitene kadar atla
                    continue
                }
            }
            i++
        }
        return out
    }

    /** tFrom'dan sonraki ilk v1→v2 geçişi; v1 = 0 ise başlangıç = kalkış anı. */
    private fun rangeSpan(c: Curve, v1: Double, v2: Double, tFrom: Double, launch: Double?): Pair<Double, Double>? {
        if (!c.ok) return null
        val i0 = lb(c.t, tFrom)
        val t1 = if (v1 <= 0) launch ?: return null else crossing(c.t, c.v, v1, i0, c.n)
        if (t1.isNaN()) return null
        val i1 = lb(c.t, t1)
        val t2 = crossing(c.t, c.v, v2, i1, c.n)
        if (t2.isNaN()) return null
        for (i in i1 until lb(c.t, t2)) if (c.v[i] < v1 - 3) return null   // arada belirgin düşüş: koşu bölünmüş
        return t1 to t2
    }

    /** tStart'tan itibaren d metreye varış anı; abort: hız tepe değerin belirgin altına düşerse koşu bozulmuş. */
    private fun timeAtDist(c: Curve, tStart: Double, d: Double, abort: Boolean): Double? {
        if (!c.inRange(tStart)) return null
        val base = c.dAt(tStart)
        var peak = interp(c.t, c.v, tStart).let { if (it.isNaN()) 0.0 else it }
        var i = lb(c.t, tStart)
        var prevT = tStart
        var prevD = 0.0
        while (i < c.n) {
            val di = c.cum[i] - base
            if (di >= d) {
                return if (di > prevD) prevT + (d - prevD) / (di - prevD) * (c.t[i] - prevT) else c.t[i]
            }
            if (abort) {
                peak = max(peak, c.v[i])
                if (c.v[i] < peak * 0.8 - 2) return null
            }
            prevT = c.t[i]; prevD = di
            i++
        }
        return null
    }

    /** tSearch'ten sonra from'u aşağı kesip < 1 km/h'e inen ilk frenleme (başlangıç ≤ tLimit). */
    private fun brakeSpan(c: Curve, from: Double, tSearch: Double, tLimit: Double): Pair<Double, Double>? {
        if (!c.ok) return null
        var i = max(lb(c.t, tSearch), 1)
        while (i < c.n) {
            if (c.v[i - 1] > from && c.v[i] <= from) {
                val t1 = c.t[i - 1] + (c.v[i - 1] - from) / (c.v[i - 1] - c.v[i]) * (c.t[i] - c.t[i - 1])
                if (t1 > tLimit) return null
                var k = i
                var broke = false
                while (k < c.n && c.v[k] >= 1.0) {
                    if (c.v[k] > from + 3) { broke = true; break }
                    k++
                }
                if (broke) { i = k; continue }
                if (k >= c.n) return null
                val t2 = c.t[k - 1] + (c.v[k - 1] - 1.0) / (c.v[k - 1] - c.v[k]) * (c.t[k] - c.t[k - 1])
                return t1 to t2
            }
            i++
        }
        return null
    }

    // --- ana akış ------------------------------------------------------------------------

    fun analyze(f: File, p: Params): Result {
        val raw = load(f)

        // GPS: esp_us yoksa en düşük RTT'li senkron ofsetiyle telefon zamanından
        var bestOff: Double? = null
        var bestRtt = Double.MAX_VALUE
        for (i in 0 until raw.sRtt.n) if (raw.sRtt[i] > 0 && raw.sRtt[i] < bestRtt) { bestRtt = raw.sRtt[i]; bestOff = raw.sOff[i] }
        val gIdx = ArrayList<Int>()
        val gTime = DoubleArray(raw.gV.n)
        for (i in 0 until raw.gV.n) {
            var t = raw.gEsp[i]
            if (t.isNaN() && bestOff != null && !raw.gPhone[i].isNaN()) t = raw.gPhone[i] + bestOff
            val v = raw.gV[i]
            if (t.isNaN() || v.isNaN() || v < 0) continue
            gTime[i] = t / 1e6
            gIdx.add(i)
        }
        gIdx.sortBy { gTime[it] }   // kararlı: zaten sıralıysa değişmez
        val gt = DoubleArray(gIdx.size) { gTime[gIdx[it]] }
        val gv = DoubleArray(gIdx.size) { raw.gV[gIdx[it]] * 3.6 }
        val gAlt = DoubleArray(gIdx.size) { raw.gAlt[gIdx[it]] }
        val gSa = DoubleArray(gIdx.size) { raw.gSa[gIdx[it]] }
        val gSat = DoubleArray(gIdx.size) { raw.gSat[gIdx[it]] }

        // IMU: yerçekimsiz boyuna ivme (m/s²) ve pitch (°); geri giden örnek atılır
        val itL = DList(raw.iT.n + 1); val iaL = DList(raw.iT.n + 1); val ipL = DList(raw.iT.n + 1)
        for (i in 0 until raw.iT.n) {
            val t = raw.iT[i] / 1e6
            if (itL.n > 0 && t < itL[itL.n - 1]) continue
            itL.add(t)
            iaL.add(raw.iAx[i] - G0 * sin(Math.toRadians(raw.iPitch[i])))
            ipL.add(raw.iPitch[i])
        }
        val imT = itL.toArray(); val ia = iaL.toArray(); val ip = ipL.toArray()

        // OBD: istek/yanıt ortası
        val otL = DList(raw.oTx.n + 1); val ovL = DList(raw.oTx.n + 1); val latMs = ArrayList<Double>(raw.oTx.n)
        for (i in 0 until raw.oTx.n) {
            val t = (raw.oTx[i] + raw.oRx[i]) / 2 / 1e6
            latMs.add((raw.oRx[i] - raw.oTx[i]) / 1000)
            if (otL.n > 0 && t < otL[otL.n - 1]) continue
            otL.add(t); ovL.add(raw.oKmh[i])
        }
        val ot = otL.toArray(); val ov = ovL.toArray()

        val firsts = listOf(gt, imT, ot).filter { it.isNotEmpty() }
        val t0 = firsts.minOfOrNull { it[0] } ?: 0.0
        val tEnd = firsts.maxOfOrNull { it.last() } ?: t0

        // füzyon ve OBD kalibrasyonu
        val ft = fuse(imT, ia, gt, gv)
        val manualK = p.obdk.takeIf { it > 0 }
        val cal = (if (gt.isNotEmpty() && ot.isNotEmpty()) obdCalib(gt, gv, ot, ov, manualK) else null)
            ?: if (manualK != null && ot.isNotEmpty()) Calib(manualK, 0.0, Double.NaN, 0, true) else null
        val oft = fuse(imT, ia, ot, ov)
        val kft = cal?.let { c -> fuse(imT, ia, DoubleArray(ot.size) { ot[it] - c.tauS }, DoubleArray(ov.size) { ov[it] / c.k }) }

        // en iyi hız eğrisi
        val gpsC = Curve(gt, gv)
        val obdC = Curve(ot, ov)
        val ftC = Curve(ft.t, ft.v)
        val oftC = Curve(oft.t, oft.v)
        val kftC = kft?.let { Curve(it.t, it.v) } ?: Curve.EMPTY
        val obdChain = listOf(kftC to "OBD+IMU", oftC to "OBD+IMU")
        val gpsChain = listOf(ftC to "GPS+IMU")
        val rawChain = if (p.src == "obd") listOf(obdC to "OBD", gpsC to "GPS") else listOf(gpsC to "GPS", obdC to "OBD")
        val order = (if (p.src == "obd") obdChain + gpsChain else gpsChain + obdChain) + rawChain
        val (best, source) = order.firstOrNull { it.first.ok } ?: (Curve.EMPTY to "—")
        val gpsBased = best === ftC || best === gpsC

        // kalkışlar: yalnız duruştan (vites geçişi değil)
        val starts = if (imT.size > 1) launches(imT, ia).filter { s ->
            val v0 = if (best.ok) interp(best.t, best.v, s) else Double.NaN
            v0.isNaN() || v0 < STOP_KMH
        } else emptyList()

        // boyuna g (~0.1 s kayan ortalama)
        val gLp = DoubleArray(imT.size)
        run {
            var s = 0.0
            var j = 0
            for (i in imT.indices) {
                s += ia[i]
                while (imT[i] - imT[j] > 0.1) { s -= ia[j]; j++ }
                gLp[i] = s / (i - j + 1) / G0
            }
        }
        fun gPeak(a: Double, b: Double, sign: Int): Double {
            var m = 0.0
            for (i in lb(imT, a) until lb(imT, b)) m = if (sign > 0) max(m, gLp[i]) else min(m, gLp[i])
            return m
        }

        // eğim: GPS rakımının mesafeye göre regresyonu (%), mesafe en iyi eğriden
        fun slopeGps(t1: Double, t2: Double): Double? {
            if (!best.ok) return null
            val xs = ArrayList<Double>()
            val ys = ArrayList<Double>()
            for (i in lb(gt, t1 - 1) until gt.size) {
                if (gt[i] > t2 + 1) break
                if (gAlt[i].isNaN()) continue
                xs.add(best.dAt(max(gt[i], t1)) - best.dAt(t1))
                ys.add(gAlt[i])
            }
            if (xs.size < 3 || xs.max() - xs.min() < 20) return null
            val mx = xs.average(); val my = ys.average()
            var sxx = 0.0; var sxy = 0.0
            for (i in xs.indices) { sxx += (xs[i] - mx) * (xs[i] - mx); sxy += (xs[i] - mx) * (ys[i] - my) }
            return if (sxx > 0) 100 * sxy / sxx else null
        }

        fun slopeImu(t1: Double, t2: Double): Double? {
            val i0 = lb(imT, t1); val i1 = lb(imT, t2)
            if (i1 - i0 < 10) return null
            var s = 0.0
            for (i in i0 until i1) s += ip[i]
            return 100 * tan(Math.toRadians(s / (i1 - i0)))
        }

        fun gpsWeak(t1: Double, t2: Double): Boolean {
            val sa = ArrayList<Double>()
            var minSat = Int.MAX_VALUE
            for (i in lb(gt, t1 - 1) until gt.size) {
                if (gt[i] > t2 + 1) break
                if (!gSa[i].isNaN() && gSa[i] >= 0) sa.add(gSa[i])
                if (!gSat[i].isNaN() && gSat[i] >= 0) minSat = min(minSat, gSat[i].toInt())
            }
            val m = median(sa)
            return (m != null && m > p.gacc) || minSat < p.sats
        }

        fun measure(tg: Target, t1: Double, t2: Double, time: Double, tGps: Double?, tObd: Double?, tObdK: Double?,
                    trap: Double?, dist: Double?): Measure {
            val sg = slopeGps(t1, t2)
            val si = slopeImu(t1, t2)
            val sl = sg ?: si
            var corr: Double? = null
            if (p.slc && sl != null && time > 0) {
                val gs = G0 * sin(atan(sl / 100))
                corr = when (tg.kind) {
                    Kind.DISTANCE -> {
                        val a = 2 * tg.distM / (time * time)
                        if (a + gs > 0) time * sqrt(a / (a + gs)) else null
                    }
                    else -> {
                        val dv = (if (tg.kind == Kind.BRAKE) -tg.from.toDouble() else (tg.to - tg.from).toDouble()) / 3.6
                        val a = dv / time
                        val den = a + gs
                        if (den != 0.0 && a / den > 0) time * a / den else null
                    }
                }
            }
            val st = when {
                gpsBased && gpsWeak(t1, t2) -> Status.GPS_WEAK
                p.slopeLimit <= 0 -> Status.OK
                sl == null -> Status.UNKNOWN
                abs(sl) > p.slopeLimit -> Status.SLOPE
                else -> Status.OK
            }
            return Measure(tg, t1, t2, time, tGps, tObd, tObdK, trap, dist, sg, si, corr, st)
        }

        /** Rollout (1 ft) açıksa süre, aracın ilk 0.3048 m'yi katettiği andan başlar. */
        fun rolloutStart(c: Curve, t1: Double): Double =
            if (p.rollout) timeAtDist(c, t1, ROLLOUT_M, false) ?: t1 else t1

        val targets = ArrayList<Target>()
        p.spd.sortedWith(compareBy({ it.first }, { it.second })).forEach { targets.add(Target(Kind.SPEED, it.first, it.second)) }
        p.dst.sorted().forEach { targets.add(Target(Kind.DISTANCE, distM = it)) }
        p.brk.sortedDescending().forEach { targets.add(Target(Kind.BRAKE, from = it)) }

        val ms = ArrayList<Measure>()
        if (best.ok) for (tg in targets) when (tg.kind) {
            Kind.SPEED -> {
                val v1 = tg.from.toDouble(); val v2 = tg.to.toDouble()
                val anchors = ArrayList<Double>()
                if (v1 <= 0) anchors.addAll(starts) else {
                    var i = 0
                    while (true) {
                        val t = crossing(best.t, best.v, v1, i, best.n)
                        if (t.isNaN()) break
                        anchors.add(t - 0.5)
                        i = lb(best.t, t) + 1
                        while (i < best.n && best.v[i] > v1 - 5) i++
                    }
                }
                for (a in anchors) {
                    val launch = if (v1 <= 0) a else null
                    fun timeOf(c: Curve): Double? {
                        val sp = rangeSpan(c, v1, v2, a, launch) ?: return null
                        val s = if (v1 <= 0) rolloutStart(c, sp.first) else sp.first
                        return sp.second - min(s, sp.second)
                    }
                    val sp = rangeSpan(best, v1, v2, a, launch) ?: continue
                    val time = timeOf(best) ?: continue
                    ms.add(measure(tg, sp.first, sp.second, time, timeOf(gpsC), timeOf(obdC), kft?.let { timeOf(kftC) },
                        null, best.dAt(sp.second) - best.dAt(sp.first)))
                }
            }
            Kind.DISTANCE -> for (s in starts) {
                fun timeOf(c: Curve): Double? {
                    val td = timeAtDist(c, s, tg.distM, true) ?: return null
                    return td - min(rolloutStart(c, s), td)
                }
                val td = timeAtDist(best, s, tg.distM, true) ?: continue
                val time = timeOf(best) ?: continue
                ms.add(measure(tg, s, td, time, timeOf(gpsC), timeOf(obdC), kft?.let { timeOf(kftC) },
                    interp(best.t, best.v, td), tg.distM))
            }
            Kind.BRAKE -> {
                val from = tg.from.toDouble()
                var tS = best.t[0]
                while (true) {
                    val sp = brakeSpan(best, from, tS, Double.MAX_VALUE) ?: break
                    tS = sp.second
                    fun timeOf(c: Curve): Double? = brakeSpan(c, from, sp.first - 1.5, sp.second)?.let { it.second - it.first }
                    ms.add(measure(tg, sp.first, sp.second, sp.second - sp.first, timeOf(gpsC), timeOf(obdC),
                        kft?.let { timeOf(kftC) }, null, best.dAt(sp.second) - best.dAt(sp.first)))
                }
            }
        }

        // koşular: en iyi eğride hareketli bölütler (≥ 3 km/h, < 2 s duruşlar birleşir)
        val segs = ArrayList<DoubleArray>()
        if (best.ok) {
            var st = Double.NaN
            for (i in 0 until best.n) {
                val moving = best.v[i] >= STOP_KMH
                if (moving && st.isNaN()) st = best.t[i]
                if (!moving && !st.isNaN()) { segs.add(doubleArrayOf(st, best.t[i])); st = Double.NaN }
            }
            if (!st.isNaN()) segs.add(doubleArrayOf(st, best.t[best.n - 1]))
        }
        val merged = ArrayList<DoubleArray>()
        for (s in segs) {
            val last = merged.lastOrNull()
            if (last != null && s[0] - last[1] < 2.0) last[1] = s[1] else merged.add(s)
        }
        val groups = LinkedHashMap<Int, MutableList<Measure>>()
        for (m in ms) {
            var bi = -1
            var bd = Double.MAX_VALUE
            for ((i, s) in merged.withIndex()) {
                val d = when {
                    m.t1 < s[0] - 3 -> s[0] - 3 - m.t1
                    m.t1 > s[1] -> m.t1 - s[1]
                    else -> 0.0
                }
                if (d < bd) { bd = d; bi = i }
            }
            groups.getOrPut(bi) { ArrayList() }.add(m)
        }
        val tIndex = targets.withIndex().associate { it.value to it.index }
        val runs = groups.entries.sortedBy { it.key }.mapIndexed { n, (si, items) ->
            val s = merged.getOrNull(si) ?: doubleArrayOf(items.minOf { it.t1 }, items.maxOf { it.t2 })
            val launch = starts.filter { it >= s[0] - 3 && it <= s[1] }.minOrNull()
            val start = launch ?: s[0]
            var vmax = 0.0
            for (i in lb(best.t, start) until lb(best.t, s[1])) vmax = max(vmax, best.v[i])
            Run(n + 1, start, s[1], launch != null, vmax, gPeak(start - 0.5, s[1], 1), gPeak(start - 0.5, s[1], -1),
                items.sortedWith(compareBy({ tIndex[it.target] ?: 0 }, { it.t1 })))
        }

        // kaynak kalitesi
        val saAll = ArrayList<Double>()
        val satAll = ArrayList<Double>()
        for (i in 0 until raw.gV.n) {
            if (!raw.gSa[i].isNaN() && raw.gSa[i] >= 0) saAll.add(raw.gSa[i])
            if (!raw.gSat[i].isNaN() && raw.gSat[i] >= 0) satAll.add(raw.gSat[i])
        }
        val rtts = (0 until raw.sRtt.n).map { raw.sRtt[it] / 1000 }
        val q = Quality(
            gt.size, rate(gt), gaps(gt, 1.5),
            median(saAll)?.let { it * 3.6 }, median(satAll), satAll.minOrNull()?.toInt(),
            imT.size, rate(imT), gaps(imT, 0.05),
            ot.size, rate(ot), gaps(ot, 1.0), median(latMs),
            raw.sRtt.n, median(rtts), rtts.minOrNull(),
            cal,
        )

        var vmax = 0.0
        for (x in best.v) vmax = max(vmax, x)
        var gmax = 0.0
        var gmin = 0.0
        for (x in gLp) { gmax = max(gmax, x); gmin = min(gmin, x) }

        return Result(
            t0, tEnd - t0, source, gpsBased,
            decimate(best.t, best.v), Series(gt, gv), Series(ot, ov), decimate(imT, gLp),
            starts, runs, targets, q, vmax, gmax, gmin, raw.events, raw.header,
        )
    }

    /** Çizim için her k'inci nokta (en çok DRAW_MAX). */
    private fun decimate(t: DoubleArray, v: DoubleArray): Series {
        if (t.size <= DRAW_MAX) return Series(t, v)
        val k = ceil(t.size.toDouble() / DRAW_MAX).toInt()
        val n = (t.size + k - 1) / k
        return Series(DoubleArray(n) { t[it * k] }, DoubleArray(n) { v[it * k] })
    }
}
