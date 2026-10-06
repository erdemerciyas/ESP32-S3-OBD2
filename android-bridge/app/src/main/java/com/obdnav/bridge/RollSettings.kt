package com.obdnav.bridge

import android.content.Context
import android.os.Handler
import android.os.Looper
import org.json.JSONObject
import java.math.BigDecimal
import java.math.RoundingMode

/** Hız aralığı (her zaman km/h); frenlemede to = 0. */
data class SpeedRange(val from: Int, val to: Int) {
    override fun toString() = "$from-$to"
}

/**
 * ROLL kişisel ayarları. Kalıcı: SharedPreferences "roll_cfg" (tek JSON).
 * ESP'ye "rcfg" mesajıyla gider (rawlog / keepdays yalnız telefonda kalır).
 */
class RollCfg {
    var src = "auto"            // auto | gps | obd
    var unit = "kmh"            // kmh | mph
    var spd = mutableListOf(SpeedRange(0, 100), SpeedRange(60, 120), SpeedRange(100, 200))
    var dst = mutableListOf(18.288, 201.168, 402.336)    // metre
    var brk = mutableListOf(100)                          // başlangıç km/h → 0
    var start = "auto"          // auto | tree
    var tree = 0.4              // pro 0.4 | sportsman 0.5 (sn)
    var ro = false              // 1 ft rollout
    var slope = 1.0             // eğim sınırı %, 0 = kapalı
    var slc = true              // eğim düzeltmeli süre
    var gacc = 1.0              // min GPS hız doğruluğu (m/s)
    var sats = 6
    var obdk = 0.0              // 0 = otomatik, aksi halde çarpan (ör. 1.03)
    var name = "Aracım"
    var mass = 1100
    var drive = "fwd"           // fwd | rwd | awd
    var beep = true
    var hold = 10               // sn, 0 = dokununca
    var rawlog = true           // telefon: ham telemetri (I/O satırları)
    var keepdays = 0            // telefon: 0 = asla silme

    /** ESP mesajı (local = true: kalıcı kayıt için telefon alanları da eklenir). */
    fun toJson(local: Boolean = false): String = buildString(420) {
        append("{\"t\":\"rcfg\",\"v\":1")
        append(",\"src\":\"").append(src).append('"')
        append(",\"unit\":\"").append(unit).append('"')
        append(",\"spd\":\"").append(spd.sortedWith(compareBy({ it.from }, { it.to })).joinToString(",")).append('"')
        append(",\"dst\":\"").append(dst.sorted().joinToString(",") { m(it) }).append('"')
        append(",\"brk\":\"").append(brk.sortedDescending().joinToString(",") { "$it-0" }).append('"')
        append(",\"start\":\"").append(start).append('"')
        append(",\"tree\":").append(Proto.f(tree, 1))
        append(",\"ro\":").append(if (ro) 1 else 0)
        append(",\"slope\":").append(Proto.f(slope, 1))
        append(",\"slc\":").append(if (slc) 1 else 0)
        append(",\"gacc\":").append(Proto.f(gacc, 1))
        append(",\"sats\":").append(sats)
        append(",\"obdk\":").append(if (obdk == 0.0) "0" else Proto.f(obdk, 3))
        append(",\"mass\":").append(mass)
        append(",\"drive\":\"").append(drive).append('"')
        append(",\"name\":").append(JSONObject.quote(cleanName(name)))
        append(",\"beep\":").append(if (beep) 1 else 0)
        append(",\"hold\":").append(hold)
        if (local) append(",\"rawlog\":").append(if (rawlog) 1 else 0).append(",\"keepdays\":").append(keepdays)
        append('}')
    }

    companion object {
        const val MAX_SPD = 10
        const val MAX_DST = 6
        const val MAX_BRK = 3

        /** En çok 3 ondalık, sondaki sıfırlar atılır (18.288, 100). */
        fun m(x: Double): String = BigDecimal.valueOf(x).setScale(3, RoundingMode.HALF_UP).stripTrailingZeros().toPlainString()

        /** Tırnak / ters bölü / denetim karakteri yok, en çok 20 karakter. */
        fun cleanName(s: String): String =
            s.filter { it != '"' && it != '\\' && !it.isISOControl() }.trim().take(20).ifEmpty { "Aracım" }

        fun parse(s: String): RollCfg {
            val c = RollCfg()
            val o = try { JSONObject(s) } catch (_: Exception) { return c }
            fun pick(k: String, ok: List<String>, def: String) = o.optString(k, def).takeIf { it in ok } ?: def
            c.src = pick("src", listOf("auto", "gps", "obd"), c.src)
            c.unit = pick("unit", listOf("kmh", "mph"), c.unit)
            if (o.has("spd")) c.spd = o.getString("spd").split(',').mapNotNull { p ->
                val ab = p.split('-')
                val a = ab.getOrNull(0)?.trim()?.toIntOrNull()
                val b = ab.getOrNull(1)?.trim()?.toIntOrNull()
                if (a != null && b != null && a in 0 until b && b <= 400) SpeedRange(a, b) else null
            }.distinct().take(MAX_SPD).toMutableList()
            if (o.has("dst")) c.dst = o.getString("dst").split(',')
                .mapNotNull { it.trim().toDoubleOrNull()?.takeIf { d -> d > 0 } }.distinct().take(MAX_DST).toMutableList()
            if (o.has("brk")) c.brk = o.getString("brk").split(',')
                .mapNotNull { it.substringBefore('-').trim().toIntOrNull()?.takeIf { v -> v in 1..400 } }
                .distinct().take(MAX_BRK).toMutableList()
            c.start = pick("start", listOf("auto", "tree"), c.start)
            c.tree = if (o.optDouble("tree", 0.4) >= 0.45) 0.5 else 0.4
            c.ro = o.optInt("ro", 0) == 1
            c.slope = o.optDouble("slope", c.slope).takeIf { it in listOf(0.0, 0.5, 1.0, 1.5, 2.0) } ?: c.slope
            c.slc = o.optInt("slc", 1) == 1
            c.gacc = o.optDouble("gacc", c.gacc).takeIf { it in listOf(0.5, 1.0, 1.5) } ?: c.gacc
            c.sats = o.optInt("sats", c.sats).takeIf { it in listOf(5, 6, 8, 10) } ?: c.sats
            c.obdk = o.optDouble("obdk", 0.0).let { if (it in 0.899..1.101) it else 0.0 }
            c.name = cleanName(o.optString("name", c.name))
            c.mass = o.optInt("mass", c.mass).coerceIn(500, 4000)
            c.drive = pick("drive", listOf("fwd", "rwd", "awd"), c.drive)
            c.beep = o.optInt("beep", 1) == 1
            c.hold = o.optInt("hold", c.hold).takeIf { it in listOf(0, 5, 10, 20) } ?: c.hold
            c.rawlog = o.optInt("rawlog", 1) == 1
            c.keepdays = o.optInt("keepdays", 0).takeIf { it in listOf(0, 30, 90) } ?: 0
            return c
        }
    }
}

/**
 * Ayar deposu + ESP senkronu. Tüm durum ana iş parçacığında (rawlog hariç:
 * TEL iş parçacığından okunur). Değişiklikler 300 ms birleştirilip gönderilir.
 */
object RollSettings {
    enum class Sync { OFFLINE, SENDING, SYNCED, FAILED, NOREPLY }

    private const val PREFS = "roll_cfg"
    private const val DEBOUNCE_MS = 300L
    private const val ACK_TIMEOUT_MS = 5000L

    private lateinit var app: Context
    private val main = Handler(Looper.getMainLooper())

    var cfg = RollCfg()
        private set
    /** Ham telemetri kaydı (Roll.onTel, binder iş parçacığı). */
    @Volatile var rawlog = true
        private set

    var sync = Sync.OFFLINE
        private set
    var why = ""
        private set
    private var outstanding = 0
    /** Ayar ekranı: senkron durumu değişti (ana iş parçacığı). */
    var listener: (() -> Unit)? = null

    fun init(ctx: Context) {
        if (::app.isInitialized) return
        app = ctx.applicationContext
        cfg = RollCfg.parse(app.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString("cfg", "") ?: "")
        rawlog = cfg.rawlog
    }

    /** cfg yerinde değiştirildikten sonra: kaydet, ardından (bağlıysa) ESP'ye gönder. */
    fun save() {
        if (!::app.isInitialized) return
        app.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString("cfg", cfg.toJson(true)).apply()
        rawlog = cfg.rawlog
        main.removeCallbacks(pushRun)
        main.postDelayed(pushRun, DEBOUNCE_MS)
    }

    fun reset() {
        cfg = RollCfg()
        save()
    }

    private val pushRun = Runnable { push() }

    private fun push() {
        if (!::app.isInitialized || BleLink.state != BleLink.State.READY) { set(Sync.OFFLINE); return }
        if (!BleLink.sendCfg(cfg.toJson())) outstanding++   // gönderilmemiş eskisinin yerini aldıysa sayı aynı
        set(Sync.SENDING)
        main.removeCallbacks(timeout)
        main.postDelayed(timeout, ACK_TIMEOUT_MS)
    }

    private val timeout = Runnable { if (sync == Sync.SENDING) set(Sync.NOREPLY) }

    // --- BleLink'ten gelenler (ana iş parçacığı) ---------------------------

    /** ESP status'u geldi (hello yanıtı / mod değişimi): bu bağlantıda henüz gönderilmediyse gönder. */
    fun onStatus() {
        if (sync == Sync.OFFLINE) push()
    }

    fun onAck(ok: Boolean, reason: String) {
        if (outstanding > 0) outstanding--
        if (outstanding > 0) return
        main.removeCallbacks(timeout)
        why = reason
        if (!ok) BridgeLog.add("ESP ROLL ayarlarını reddetti ($reason)")
        set(if (ok) Sync.SYNCED else Sync.FAILED)
    }

    fun onLinkDown() {
        outstanding = 0
        main.removeCallbacks(timeout)
        set(Sync.OFFLINE)
    }

    private fun set(s: Sync) {
        sync = s
        listener?.invoke()
    }
}
