package com.obdnav.bridge

import android.content.Context
import android.location.Location
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.concurrent.Executors

/**
 * Sürüş kaydı: navigasyon başlayınca açılır, bitince kapanır ve
 * files/trips/trip_<başlangıç>.json olarak saklanır. Yalnızca ana iş
 * parçacığından çağrılır; dosya yazımı arka planda.
 */
object TripRecorder {
    class Pt(val lat: Double, val lon: Double, val t: Long, val spd: Float)
    class AlertRec(val t: Long, val kind: String, val text: String, val lat: Double, val lon: Double)

    class Trip(val start: Long, var src: String) {
        var end = 0L
        var distM = 0.0
        val pts = ArrayList<Pt>()
        val roads = ArrayList<String>()
        val alerts = ArrayList<AlertRec>()
        val durationS: Long get() = ((if (end > 0) end else System.currentTimeMillis()) - start) / 1000
    }

    private const val MIN_STEP_M = 8f
    private const val AUTOSAVE_MS = 60_000L
    private val io = Executors.newSingleThreadExecutor()
    private var lastSave = 0L

    var current: Trip? = null
        private set
    private var last: Location? = null

    private fun dir(ctx: Context) = File(ctx.filesDir, "trips").apply { mkdirs() }

    fun start(src: String) {
        if (current != null) return
        current = Trip(System.currentTimeMillis(), src)
        last = null
        BridgeLog.add("Sürüş kaydı başladı")
    }

    fun stop(ctx: Context) {
        val t = current ?: return
        current = null
        t.end = System.currentTimeMillis()
        if (t.pts.size >= 2) {
            save(ctx, t)
            BridgeLog.add("Sürüş kaydedildi: ${"%.1f".format(t.distM / 1000)} km")
        } else {
            BridgeLog.add("Sürüş çok kısa, kaydedilmedi")
        }
    }

    fun addPoint(ctx: Context, loc: Location) {
        val t = current ?: return
        val prev = last
        if (prev != null) {
            val d = prev.distanceTo(loc)
            if (d < MIN_STEP_M) return
            t.distM += d
        }
        last = loc
        t.pts += Pt(loc.latitude, loc.longitude, loc.time, if (loc.hasSpeed()) loc.speed * 3.6f else -1f)
        val now = System.currentTimeMillis()
        if (now - lastSave > AUTOSAVE_MS) {
            lastSave = now
            save(ctx, t)
        }
    }

    fun addRoad(road: String?) {
        val t = current ?: return
        if (road.isNullOrBlank() || t.roads.lastOrNull() == road) return
        t.roads += road
    }

    fun addAlert(kind: String, text: String) {
        val t = current ?: return
        if (t.alerts.lastOrNull()?.let { it.kind == kind && it.text == text } == true) return
        val l = last
        t.alerts += AlertRec(System.currentTimeMillis(), kind, text, l?.latitude ?: 0.0, l?.longitude ?: 0.0)
    }

    // --- dosya -------------------------------------------------------------------

    private fun toJson(t: Trip): String {
        val pts = JSONArray()
        for (p in t.pts) pts.put(JSONArray().put(p.lat).put(p.lon).put(p.t).put(p.spd.toDouble()))
        val alerts = JSONArray()
        for (a in t.alerts) alerts.put(JSONObject().put("t", a.t).put("k", a.kind).put("txt", a.text)
            .put("lat", a.lat).put("lon", a.lon))
        return JSONObject().put("start", t.start).put("end", t.end).put("src", t.src).put("dist", t.distM)
            .put("roads", JSONArray(t.roads)).put("pts", pts).put("alerts", alerts).toString()
    }

    private fun save(ctx: Context, t: Trip) {
        val json = toJson(t)   // ana iş parçacığında kopya; listeler sonra değişebilir
        val f = File(dir(ctx), "trip_${t.start}.json")
        io.execute { f.writeText(json) }
    }

    fun list(ctx: Context): List<File> =
        dir(ctx).listFiles { f -> f.name.endsWith(".json") }?.sortedByDescending { it.name } ?: emptyList()

    fun load(f: File): Trip? = try {
        val o = JSONObject(f.readText())
        Trip(o.getLong("start"), o.optString("src")).apply {
            end = o.optLong("end")
            distM = o.optDouble("dist", 0.0)
            o.optJSONArray("roads")?.let { a -> for (i in 0 until a.length()) roads += a.getString(i) }
            o.optJSONArray("pts")?.let { a ->
                for (i in 0 until a.length()) {
                    val p = a.getJSONArray(i)
                    pts += Pt(p.getDouble(0), p.getDouble(1), p.getLong(2), p.getDouble(3).toFloat())
                }
            }
            o.optJSONArray("alerts")?.let { a ->
                for (i in 0 until a.length()) {
                    val x = a.getJSONObject(i)
                    alerts += AlertRec(x.getLong("t"), x.getString("k"), x.optString("txt"),
                        x.optDouble("lat"), x.optDouble("lon"))
                }
            }
            if (end == 0L) end = pts.lastOrNull()?.t ?: start
        }
    } catch (e: Exception) {
        null
    }
}
