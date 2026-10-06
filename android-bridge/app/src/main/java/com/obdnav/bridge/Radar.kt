package com.obdnav.bridge

import android.content.Context
import android.location.Location
import android.os.Handler
import android.os.Looper
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import java.util.concurrent.Executors
import kotlin.math.abs

/**
 * Kendi radar uyarımız: OpenStreetMap "highway=speed_camera" noktaları
 * (Overpass API, konum çevresi ±0.3°, diskte önbellek) + telefon GPS'i.
 * Gidiş yönünde WARN_M içindeki en yakın kamera için ESP'ye uyarı gönderir;
 * geçince uyarıyı kaldırır. Navigasyon uygulamasından bağımsız çalışır.
 */
object Radar {
    class Cam(val lat: Double, val lon: Double, val limit: Int?)

    private const val WARN_M = 600f
    private const val AHEAD_DEG = 40f
    private const val BOX = 0.3            // derece (~33 km)
    private const val EDGE = 0.08          // kenara bu kadar yaklaşınca yenile
    private const val RETRY_MS = 5 * 60_000L

    private val main = Handler(Looper.getMainLooper())
    private val io = Executors.newSingleThreadExecutor()
    private var cams: List<Cam> = emptyList()
    private var box: DoubleArray? = null    // s, w, n, e
    private var loaded = false
    private var fetching = false
    private var lastFail = 0L
    private var active: Cam? = null

    val count: Int get() = cams.size

    private fun file(ctx: Context) = File(ctx.filesDir, "cameras.json")

    /** Konum geldikçe (ana iş parçacığı). Uyarı JSON'u veya null döner. */
    fun check(ctx: Context, loc: Location): String? {
        if (!loaded) load(ctx)
        ensureData(ctx, loc.latitude, loc.longitude)

        var best: Cam? = null
        var bestD = Float.MAX_VALUE
        val moving = loc.hasBearing() && loc.hasSpeed() && loc.speed > 3f
        val res = FloatArray(2)
        for (c in cams) {
            if (abs(c.lat - loc.latitude) > 0.01 || abs(c.lon - loc.longitude) > 0.013) continue
            Location.distanceBetween(loc.latitude, loc.longitude, c.lat, c.lon, res)
            val d = res[0]
            if (d > WARN_M || d >= bestD) continue
            if (moving) {
                var diff = abs(res[1] - loc.bearing) % 360f
                if (diff > 180f) diff = 360f - diff
                if (diff > AHEAD_DEG) continue   // arkada ya da yan yolda
            }
            best = c
            bestD = d
        }

        if (best != null) {
            if (active !== best) {
                active = best
                TripRecorder.addAlert("cam", "Radar" + (best.limit?.let { " $it km/h" } ?: ""))
            }
            return Proto.alert("cam", "Radar", ((bestD / 10).toInt() * 10), best.limit)
        }
        if (active != null) {
            active = null
            return Proto.alert("none")
        }
        return null
    }

    private fun load(ctx: Context) {
        loaded = true
        try {
            val o = JSONObject(file(ctx).readText())
            val b = o.getJSONArray("box")
            box = DoubleArray(4) { b.getDouble(it) }
            cams = parse(o.getJSONArray("cams"))
        } catch (_: Exception) { }
    }

    private fun parse(a: JSONArray): List<Cam> = (0 until a.length()).map {
        val c = a.getJSONArray(it)
        Cam(c.getDouble(0), c.getDouble(1), if (c.length() > 2 && !c.isNull(2)) c.getInt(2) else null)
    }

    private fun ensureData(ctx: Context, lat: Double, lon: Double) {
        val b = box
        val inside = b != null && lat > b[0] + EDGE && lon > b[1] + EDGE && lat < b[2] - EDGE && lon < b[3] - EDGE
        if (inside || fetching || System.currentTimeMillis() - lastFail < RETRY_MS) return
        fetching = true
        val nb = doubleArrayOf(lat - BOX, lon - BOX, lat + BOX, lon + BOX)
        val app = ctx.applicationContext
        io.execute {
            val result = try { fetch(nb) } catch (e: Exception) { null }
            main.post {
                fetching = false
                if (result == null) {
                    lastFail = System.currentTimeMillis()
                    BridgeLog.add("Radar verisi alınamadı (internet?)")
                    return@post
                }
                cams = result
                box = nb
                BridgeLog.add("Radar verisi: ${result.size} kamera")
                val arr = JSONArray()
                for (c in result) arr.put(JSONArray().put(c.lat).put(c.lon).put(c.limit ?: JSONObject.NULL))
                io.execute {
                    file(app).writeText(JSONObject().put("box", JSONArray(nb.toList())).put("cams", arr).toString())
                }
            }
        }
    }

    private fun fetch(b: DoubleArray): List<Cam> {
        val q = "[out:json][timeout:25];node[\"highway\"=\"speed_camera\"](${b[0]},${b[1]},${b[2]},${b[3]});out;"
        val c = URL("https://overpass-api.de/api/interpreter?data=" + URLEncoder.encode(q, "UTF-8"))
            .openConnection() as HttpURLConnection
        c.setRequestProperty("User-Agent", "AURA-Bridge/0.7")
        c.connectTimeout = 15000
        c.readTimeout = 30000
        val els = JSONObject(c.inputStream.use { it.readBytes().toString(Charsets.UTF_8) }).getJSONArray("elements")
        return (0 until els.length()).map {
            val e = els.getJSONObject(it)
            val lim = e.optJSONObject("tags")?.optString("maxspeed")?.takeWhile { ch -> ch.isDigit() }?.toIntOrNull()
            Cam(e.getDouble("lat"), e.getDouble("lon"), lim)
        }
    }
}
