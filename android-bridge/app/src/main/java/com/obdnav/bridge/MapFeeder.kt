package com.obdnav.bridge

import android.content.Context
import android.graphics.Bitmap
import android.os.Handler
import android.os.Looper
import java.io.ByteArrayOutputStream
import java.util.concurrent.Executors
import kotlin.math.hypot

/**
 * ESP harita sayfası için karanlık harita resmi üretip gönderir. İki mod
 * (ESP "mapreq" ile değiştirir):
 *  - FIT (varsayılan): sürüş kaydının tamamı + konum ekrana sığar; rota
 *    büyüdükçe zoom kendiliğinden azalır.
 *  - FOLLOW: araca ortalı, ESP'de seçilen zoom'da; araç uzaklaşınca yenilenir.
 * ESP rota çizgisini ve konumu aynı merkez + zoom ile kendisi çizer.
 */
object MapFeeder {
    const val SIZE = 460              // ESP görünür daire çapı
    private const val FIT_PAD = 72    // daire içine sığsın (köşeler görünmez)
    private const val FIT_MAX_ZOOM = 17
    private const val DEFAULT_ZOOM = 16
    private const val FOLLOW_MOVE_PX = 110
    private const val FIT_SHIFT_PX = 60
    private const val MIN_INTERVAL_MS = 4000L
    private const val QUALITY = 70

    private val main = Handler(Looper.getMainLooper())
    private val worker = Executors.newSingleThreadExecutor()
    private var app: Context? = null
    private var fit = true
    private var followZoom = DEFAULT_ZOOM
    private var sentCenter: DoubleArray? = null   // gönderilen resmin merkezi (dünya px, sentZoom)
    private var sentZoom = -1
    private var lastRender = 0L
    private var force = false
    private var busy = false
    private var id = 0

    @Volatile
    var lastInfo = "-"
        private set

    val modeText: String get() = if (fit) "tüm rota" else "zoom $followZoom"

    fun reset() {
        sentCenter = null
        force = true
    }

    /** ESP'den: {"t":"mapreq","mode":"fit"|"follow","z":16} */
    fun request(fitMode: Boolean, zoom: Int) {
        fit = fitMode
        if (!fitMode && zoom in 3..19) followZoom = zoom
        force = true
        BridgeLog.add("ESP harita istedi: $modeText")
        val c = app
        val fix = TripService.lastFix
        if (c != null && fix != null) onLocation(c, fix.latitude, fix.longitude)
    }

    /** ESP'nin harita sonucu (mapack). Başarısızsa bir sonraki konumda yeniden gönder. */
    fun onAck(ackId: Int, ok: Boolean, why: String) {
        if (ok) {
            lastInfo = lastInfo.replace("gönderiliyor", "ESP'de")
            BridgeLog.add("Harita #$ackId ESP'de gösterime hazır")
        } else {
            lastInfo = "#$ackId ESP reddetti: $why"
            BridgeLog.add("Harita #$ackId ESP'de başarısız: $why")
            if (ackId == id) force = true
        }
    }

    /** Hedef: merkez (lat, lon) ve zoom. */
    private fun target(lat: Double, lon: Double): Triple<Double, Double, Int> {
        if (!fit) return Triple(lat, lon, followZoom)
        val pts = TripRecorder.current?.pts
        if (pts == null || pts.size < 2) return Triple(lat, lon, DEFAULT_ZOOM)
        var minLat = lat; var maxLat = lat; var minLon = lon; var maxLon = lon
        for (p in pts) {
            if (p.lat < minLat) minLat = p.lat
            if (p.lat > maxLat) maxLat = p.lat
            if (p.lon < minLon) minLon = p.lon
            if (p.lon > maxLon) maxLon = p.lon
        }
        val z = MapRenderer.fitZoom(minLat, minLon, maxLat, maxLon, SIZE, SIZE, FIT_PAD).coerceAtMost(FIT_MAX_ZOOM)
        return Triple((minLat + maxLat) / 2, (minLon + maxLon) / 2, z)
    }

    /** Ana iş parçacığından, her konumda. */
    fun onLocation(ctx: Context, lat: Double, lon: Double) {
        app = ctx.applicationContext
        if (BleLink.state != BleLink.State.READY || busy || BleLink.mapBusy) return
        val now = System.currentTimeMillis()
        if (!force && now - lastRender < MIN_INTERVAL_MS) return

        val (cLat, cLon, z) = target(lat, lon)
        val w = MapRenderer.world(cLat, cLon, z)
        val c = sentCenter
        val need = force || c == null || z != sentZoom ||
            (fit && hypot(w[0] - c[0], w[1] - c[1]) > FIT_SHIFT_PX) ||
            (!fit && hypot(w[0] - c[0], w[1] - c[1]) > FOLLOW_MOVE_PX)
        if (!need) return

        force = false
        busy = true
        lastRender = now
        val appCtx = ctx.applicationContext
        worker.execute {
            val t0 = System.currentTimeMillis()
            val bmp = MapRenderer.render(appCtx, w[0], w[1], z, SIZE, SIZE, dark = true)
            val out = ByteArrayOutputStream()
            bmp.compress(Bitmap.CompressFormat.JPEG, QUALITY, out)
            bmp.recycle()
            val jpg = out.toByteArray()
            main.post {
                id = (id + 1) and 0xFF
                BleLink.sendMap(id, jpg, cLat, cLon, z, SIZE, SIZE)
                sentCenter = w
                sentZoom = z
                busy = false
                lastInfo = "#$id z$z ${jpg.size / 1024} KB, ${MapRenderer.lastTiles} — gönderiliyor"
                BridgeLog.add("Harita #$id gönderiliyor ($modeText, z$z, ${jpg.size / 1024} KB, " +
                    "${MapRenderer.lastTiles}, ${System.currentTimeMillis() - t0} ms)")
                if (MapRenderer.lastTiles.startsWith("0/")) BridgeLog.add("Harita karoları indirilemedi — internet?")
            }
        }
    }
}
