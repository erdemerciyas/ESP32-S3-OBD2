package com.obdnav.bridge

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.location.GnssStatus
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.os.SystemClock
import java.io.BufferedWriter
import java.io.File
import java.io.FileWriter
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * ROLL (performans zamanlayıcı) — telefon tarafı F0: en hızlı GNSS kaynağı,
 * ESP saat senkronu ve ham veri kaydı (filesDir/roll altında CSV).
 *
 * Etkin: BLE hazır && ESP modu "roll" && TripService çalışıyor. Durum ana iş
 * parçacığında; TEL çözümü binder iş parçacığında, dosya yazımı "roll-io"da.
 */
@SuppressLint("MissingPermission")
object Roll {
    private const val PING_MS = 1000L
    private const val SYNC_KEEP = 30
    private const val SYNC_RTT_OK_US = 150_000L
    private const val SYNC_MIN_OK = 3
    private const val APPEND_GAP_MS = 60_000L

    private lateinit var app: Context
    private val main = Handler(Looper.getMainLooper())
    private val io: Handler by lazy { Handler(HandlerThread("roll-io").apply { start() }.looper) }

    /** ESP'nin bildirdiği mod: "obd" | "nav" | "roll"; bağlı değilse null, eski firmware "". */
    @Volatile var espMode: String? = null
        private set
    @Volatile var active = false
        private set

    // --- saat senkronu (ESP µs = telefon µs + offsetUs) ---------------------
    private class Sample(val offset: Long, val rtt: Long)
    private val samples = ArrayDeque<Sample>()
    @Volatile var offsetUs = 0L
        private set
    @Volatile var rttUs = -1L          // en iyi (en kısa RTT) örneğin RTT'si
        private set
    @Volatile var syncGood = 0         // RTT < 150 ms örnek sayısı
        private set
    @Volatile var syncValid = false
        private set
    @Volatile private var espRef = -1L   // son bilinen 64-bit ESP zamanı (pong "e"), 32-bit açmak için
    private var pingSeq = 0

    // --- GNSS / telemetri durumu (ekran için) -------------------------------
    @Volatile var lastSpeed = -1f      // m/s
        private set
    @Volatile var lastSa = -1f
        private set
    @Volatile var sats = -1
        private set
    @Volatile var lastObdKmh = -1
        private set
    val gpsRate = RateMeter()
    val imuRate = RateMeter()
    val obdRate = RateMeter()

    // --- kayıt dosyası --------------------------------------------------------
    @Volatile var file: File? = null
        private set
    @Volatile var fileBytes = 0L
        private set
    private var writer: BufferedWriter? = null   // yalnız io iş parçacığı
    private var lastFile: File? = null
    private var closedAt = 0L

    fun init(ctx: Context) {
        if (::app.isInitialized) return
        app = ctx.applicationContext
        RollSettings.init(app)
        purgeOld()
    }

    /** Ayar "otomatik silme": N günden eski roll_*.csv dosyaları (süren kayıt hariç), io iş parçacığında. */
    fun purgeOld() {
        if (!::app.isInitialized) return
        val days = RollSettings.cfg.keepdays
        if (days <= 0) return
        val limit = System.currentTimeMillis() - days * 86_400_000L
        io.post {
            val old = dir(app).listFiles { f ->
                f.name.startsWith("roll_") && f.name.endsWith(".csv") && f != file && f.lastModified() < limit
            } ?: return@post
            old.forEach { it.delete() }
            if (old.isNotEmpty()) BridgeLog.add("ROLL: ${old.size} eski kayıt silindi (> $days gün)")
        }
    }

    fun dir(ctx: Context) = File(ctx.filesDir, "roll").apply { mkdirs() }

    fun nowUs() = SystemClock.elapsedRealtimeNanos() / 1000

    // --- BleLink'ten gelenler -----------------------------------------------

    /** status yanıtı / mod değişimi (ana iş parçacığı). */
    fun onStatus(mode: String) {
        if (mode == espMode) return
        espMode = mode
        if (mode == "roll") {
            startPings()
            if (::app.isInitialized && !TripService.running) {
                if (app.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED) {
                    BridgeLog.add("ROLL: köprü başlatılıyor")
                    TripService.start(app)   // arka planda reddedilebilir → aşağıdaki ipucu
                }
                main.postDelayed({
                    if (espMode == "roll" && !TripService.running) BridgeLog.add("ROLL için köprüyü başlat")
                }, 2000)
            }
        }
        evaluate()
    }

    /** pong: m = ping'deki telefon µs, e = ESP µs, t2 = varış (telefon µs). Ana iş parçacığı. */
    fun onPong(m: Long, e: Long, t2: Long) {
        espRef = e
        val rtt = t2 - m
        if (rtt < 0 || rtt > 5_000_000) return
        val off = e - (m + t2) / 2
        samples.addLast(Sample(off, rtt))
        while (samples.size > SYNC_KEEP) samples.removeFirst()
        val best = samples.minBy { it.rtt }
        offsetUs = best.offset
        rttUs = best.rtt
        syncGood = samples.count { it.rtt < SYNC_RTT_OK_US }
        syncValid = syncGood >= SYNC_MIN_OK
        if (active) write("S,$t2,$off,$rtt")
    }

    /** BLE koptu / kapandı (ana iş parçacığı). ESP yeniden açılmış olabilir: senkron sıfırlanır. */
    fun onLinkDown() {
        espMode = null
        samples.clear()
        syncValid = false
        syncGood = 0
        rttUs = -1
        espRef = -1
        evaluate()
    }

    /**
     * TEL bildirimi (binder iş parçacığı). [tip][n][kayıtlar], LE.
     * 0x01 IMU 14 B: u32 t, i16 ax ay az (mm/s²), i16 gz (0.01°/s), i16 pitch (0.01°)
     * 0x02 OBD 9 B: u32 t_tx, u32 t_rx, u8 km/h
     */
    fun onTel(b: ByteArray) {
        if (b.size < 2) return
        val type = b[0].toInt() and 0xFF
        val size = when (type) { 1 -> 14; 2 -> 9; else -> return }
        val n = minOf(b[1].toInt() and 0xFF, (b.size - 2) / size)
        if (n <= 0) return
        val bb = ByteBuffer.wrap(b, 2, n * size).order(ByteOrder.LITTLE_ENDIAN)
        if (type == 1) imuRate.add(n) else {
            obdRate.add(n)
            lastObdKmh = b[2 + (n - 1) * size + 8].toInt() and 0xFF
        }
        val ref = espRef
        if (!active || ref < 0) return   // 64-bit referans yok: zaman açılamaz
        if (!RollSettings.rawlog) return   // ayar: ham telemetri (I/O) kaydedilmez
        val sb = StringBuilder(n * 40)
        repeat(n) {
            if (type == 1) {
                sb.append("I,").append(unwrap(bb.int, ref))
                repeat(5) { sb.append(',').append(bb.short.toInt()) }
            } else {
                sb.append("O,").append(unwrap(bb.int, ref)).append(',').append(unwrap(bb.int, ref))
                    .append(',').append(bb.get().toInt() and 0xFF)
            }
            sb.append('\n')
        }
        writeRaw(sb.toString())
    }

    /** 32-bit ESP zamanını en yakın 64-bit değere aç (işaretli fark). */
    private fun unwrap(t32: Int, ref: Long): Long = ref + (t32 - ref.toInt())

    // --- etkinleştirme ------------------------------------------------------

    /** Etkinlik koşulunu yeniden değerlendir (ana iş parçacığı). */
    fun evaluate() {
        if (!::app.isInitialized) return
        val want = BleLink.state == BleLink.State.READY && espMode == "roll" && TripService.running
        if (want == active) return
        if (want) activate() else deactivate(
            when {
                espMode == null || BleLink.state != BleLink.State.READY -> "stop ble-down"
                espMode != "roll" -> "stop mode=${espMode}"
                else -> "stop service-off"
            }
        )
    }

    private fun activate() {
        active = true
        gpsRate.reset(); imuRate.reset(); obdRate.reset()
        lastSpeed = -1f; lastSa = -1f
        openFile()
        val lm = app.getSystemService(LocationManager::class.java)
        try {
            lm.requestLocationUpdates(LocationManager.GPS_PROVIDER, 0L, 0f, gpsListener, Looper.getMainLooper())
            lm.registerGnssStatusCallback(gnssCb, main)
        } catch (e: Exception) {
            BridgeLog.add("ROLL: GPS açılamadı (${e.javaClass.simpleName})")
        }
        startPings()
        BridgeLog.add("ROLL kaydı: ${file?.name}")
    }

    private fun deactivate(reason: String) {
        active = false
        val lm = app.getSystemService(LocationManager::class.java)
        try {
            lm.removeUpdates(gpsListener)
            lm.unregisterGnssStatusCallback(gnssCb)
        } catch (_: Exception) { }
        sats = -1
        closeFile(reason)
        BridgeLog.add("ROLL kaydı durdu ($reason)")
    }

    private val pinger = object : Runnable {
        override fun run() {
            if (BleLink.state != BleLink.State.READY || espMode != "roll") return
            BleLink.send(Proto.ping(++pingSeq))
            main.postDelayed(this, PING_MS)
        }
    }

    private fun startPings() {
        main.removeCallbacks(pinger)
        main.post(pinger)
    }

    // --- GNSS -----------------------------------------------------------------

    private val gpsListener = object : LocationListener {
        override fun onLocationChanged(loc: Location) = onFix(loc)
        @Deprecated("API < 29")
        override fun onStatusChanged(provider: String?, status: Int, extras: Bundle?) { }
        override fun onProviderEnabled(provider: String) { }
        override fun onProviderDisabled(provider: String) { }
    }

    private val gnssCb = object : GnssStatus.Callback() {
        override fun onSatelliteStatusChanged(status: GnssStatus) {
            var used = 0
            for (i in 0 until status.satelliteCount) if (status.usedInFix(i)) used++
            sats = used
        }
    }

    private fun onFix(loc: Location) {
        if (!active) return
        val phoneUs = loc.elapsedRealtimeNanos / 1000
        val esp = if (syncValid) phoneUs + offsetUs else null
        val v = if (loc.hasSpeed()) loc.speed else -1f
        val sa = if (loc.hasSpeedAccuracy()) loc.speedAccuracyMetersPerSecond else -1f
        val alt = when {
            Build.VERSION.SDK_INT >= 34 && loc.hasMslAltitude() -> loc.mslAltitudeMeters
            loc.hasAltitude() -> loc.altitude
            else -> null
        }
        val va = if (loc.hasVerticalAccuracy()) loc.verticalAccuracyMeters else -1f
        val ha = if (loc.hasAccuracy()) loc.accuracy else -1f
        val hdg = if (loc.hasBearing()) loc.bearing else -1f
        val sat = sats
        BleLink.send(Proto.gnss(esp, v, sa, alt, va, ha, hdg, sat, loc.latitude, loc.longitude))
        write("G,${esp ?: ""},$phoneUs,${Proto.f(v, 2)},${Proto.f(sa, 2)},${alt?.let { Proto.f(it, 1) } ?: ""}," +
            "${Proto.f(va, 1)},${Proto.f(ha, 1)},${Proto.f(hdg, 1)},$sat,${Proto.f(loc.latitude, 6)},${Proto.f(loc.longitude, 6)}")
        gpsRate.add(1)
        lastSpeed = v
        lastSa = sa
    }

    // --- dosya ------------------------------------------------------------------

    private fun openFile() {
        val prev = lastFile
        val resume = prev != null && prev.exists() && SystemClock.elapsedRealtime() - closedAt < APPEND_GAP_MS
        val f = if (resume) prev!! else
            File(dir(app), "roll_${SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())}.csv")
        file = f
        val header = if (resume) null else buildString {
            append("# AURA ROLL log v1, app ${version()}, device ${Build.MODEL}, android ${Build.VERSION.SDK_INT}\n")
            append("# G,esp_us,phone_us,v_mps,sa_mps,alt_m,va_m,ha_m,hdg_deg,sat,lat,lon\n")
            append("# I,esp_us,ax_mmps2,ay_mmps2,az_mmps2,gz_cdps,pitch_cdeg\n")
            append("# O,esp_tx_us,esp_rx_us,kmh\n")
            append("# S,phone_us,offset_us,rtt_us\n")
            append("# M,phone_us,event\n")
        }
        val event = "M,${nowUs()},${if (resume) "resume" else "start"}\n"
        io.post {
            try {
                writer = BufferedWriter(FileWriter(f, true), 64 * 1024)
                fileBytes = f.length()
                header?.let { rawIo(it) }
                rawIo(event)
            } catch (e: Exception) {
                writer = null
                BridgeLog.add("ROLL: dosya açılamadı (${e.javaClass.simpleName})")
            }
        }
        io.removeCallbacks(flusher)
        io.postDelayed(flusher, 1000)
    }

    private fun closeFile(reason: String) {
        write("M,${nowUs()},$reason")
        io.post {
            try { writer?.close() } catch (_: Exception) { }
            writer = null
        }
        lastFile = file
        closedAt = SystemClock.elapsedRealtime()
        file = null
    }

    private val flusher = object : Runnable {
        override fun run() {
            val w = writer ?: return
            try { w.flush() } catch (_: Exception) { }
            io.postDelayed(this, 1000)
        }
    }

    /** Diske bas, sonra r'yi io iş parçacığında çalıştır (dışa aktarım öncesi). */
    fun afterFlush(r: () -> Unit) {
        io.post {
            try { writer?.flush() } catch (_: Exception) { }
            r()
        }
    }

    private fun write(line: String) = writeRaw(line + "\n")

    private fun writeRaw(s: String) {
        io.post { rawIo(s) }
    }

    private fun rawIo(s: String) {
        val w = writer ?: return
        try {
            w.write(s)
            fileBytes += s.length   // ASCII
        } catch (_: Exception) { }
    }

    private fun version(): String = try {
        app.packageManager.getPackageInfo(app.packageName, 0).versionName ?: "?"
    } catch (_: Exception) { "?" }

    fun sizeText(bytes: Long): String = when {
        bytes >= 1024 * 1024 -> "%.1f MB".format(bytes / 1048576.0)
        bytes >= 1024 -> "${bytes / 1024} KB"
        else -> "$bytes B"
    }

    /** Son 5 sn'deki olay hızı (kayıt/sn). Her iş parçacığından güvenli. */
    class RateMeter(private val windowMs: Long = 5000) {
        private val times = ArrayDeque<Long>()
        private val counts = ArrayDeque<Int>()
        private var sum = 0

        @Synchronized
        fun add(n: Int) {
            val now = SystemClock.elapsedRealtime()
            times.addLast(now)
            counts.addLast(n)
            sum += n
            prune(now)
        }

        @Synchronized
        fun rate(): Float {
            prune(SystemClock.elapsedRealtime())
            return sum * 1000f / windowMs
        }

        @Synchronized
        fun reset() {
            times.clear(); counts.clear(); sum = 0
        }

        private fun prune(now: Long) {
            while (times.isNotEmpty() && now - times.first() > windowMs) {
                times.removeFirst()
                sum -= counts.removeFirst()
            }
        }
    }
}
