package com.obdnav.bridge

import android.os.SystemClock
import org.json.JSONObject
import java.util.Locale

/** Navigasyon verisi — null alanlar ESP'ye gönderilmez (ekrandaki değer korunur). */
data class NavData(
    val maneuver: String?,      // S L R SL SR U RB A
    val distM: Int?,
    val remainM: Int?,
    val etaMin: Int?,           // gece yarısından dakika
    val road: String?,
    val src: String,
)

/** ESP32 protokol v1 (JSON) — firmware: main/nav/nav_codec_json.c */
object Proto {
    fun hello(src: String) = withTime(JSONObject().put("t", "hello").put("v", 1).put("src", src)).toString()
    fun start() = JSONObject().put("t", "start").toString()
    fun stop() = JSONObject().put("t", "stop").toString()
    /** m = telefon monotonik saati (µs); ESP pong'da geri yollar → saat senkronu (Roll). */
    fun ping(seq: Int) = withTime(JSONObject().put("t", "ping").put("seq", seq))
        .put("m", SystemClock.elapsedRealtimeNanos() / 1000).toString()

    /**
     * ROLL: tek GNSS ölçümü. e = ölçüm anı (ESP µs; senkron yoksa yok),
     * bilinmeyen alanlar -1 (alt bilinmiyorsa hiç yazılmaz). Tek BLE yazımına sığar (~150 B).
     */
    fun gnss(espUs: Long?, v: Float, sa: Float, alt: Double?, va: Float, ha: Float, hdg: Float,
             sat: Int, lat: Double, lon: Double): String {
        val sb = StringBuilder(160).append("{\"t\":\"gnss\"")
        espUs?.let { sb.append(",\"e\":").append(it) }
        sb.append(",\"v\":").append(f(v, 2)).append(",\"sa\":").append(f(sa, 2))
        alt?.let { sb.append(",\"alt\":").append(f(it, 1)) }
        sb.append(",\"va\":").append(f(va, 1)).append(",\"ha\":").append(f(ha, 1))
            .append(",\"hdg\":").append(f(hdg, 1)).append(",\"sat\":").append(sat)
            .append(",\"lat\":").append(f(lat, 6)).append(",\"lon\":").append(f(lon, 6)).append('}')
        return sb.toString()
    }

    /** Noktalı ondalık (yerel ayardan bağımsız) — JSON ve CSV için. */
    fun f(x: Number, dec: Int): String = String.format(Locale.US, "%.${dec}f", x.toDouble())

    /** ESP saat senkronu: ts = UTC epoch saniye, tz = yerel ofset (dakika, yaz saati dahil). */
    private fun withTime(o: JSONObject): JSONObject {
        val now = System.currentTimeMillis()
        return o.put("ts", now / 1000).put("tz", java.util.TimeZone.getDefault().getOffset(now) / 60000)
    }

    fun loc(lat: Double, lon: Double, speedKmh: Int?, heading: Int?): String {
        val o = JSONObject().put("t", "loc")
            .put("lat", Math.round(lat * 1e6) / 1e6)
            .put("lon", Math.round(lon * 1e6) / 1e6)
        speedKmh?.let { o.put("spd", it) }
        heading?.let { o.put("hdg", it) }
        return o.toString()
    }

    /** kind: cam traffic hazard info none */
    fun alert(kind: String, text: String? = null, distM: Int? = null, limitKmh: Int? = null): String {
        val o = JSONObject().put("t", "alert").put("k", kind)
        text?.let { o.put("txt", it.take(44)) }
        distM?.let { o.put("d", it) }
        limitKmh?.let { o.put("lim", it) }
        return o.toString()
    }

    fun mapHeader(id: Int, len: Int, w: Int, h: Int, lat: Double, lon: Double, zoom: Int): String =
        JSONObject().put("t", "map").put("id", id).put("len", len).put("w", w).put("h", h)
            .put("clat", lat).put("clon", lon).put("z", zoom).toString()

    fun update(seq: Int, d: NavData, speedKmh: Int? = null): String {
        val o = JSONObject().put("t", "upd").put("seq", seq)
        d.maneuver?.let { o.put("m", it) }
        d.distM?.let { o.put("md", it) }
        d.remainM?.let { o.put("rd", it) }
        d.etaMin?.let { o.put("eta", it) }
        d.road?.let { o.put("next", it) }
        speedKmh?.let { o.put("spd", it) }
        return o.toString()
    }
}
