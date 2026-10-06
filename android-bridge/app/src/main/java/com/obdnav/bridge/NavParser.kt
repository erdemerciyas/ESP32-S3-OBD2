package com.obdnav.bridge

import android.app.Notification
import android.content.Context
import android.service.notification.StatusBarNotification
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.TextView
import java.util.Locale

/**
 * Navigasyon bildirimlerinden manevra / mesafe / ETA / yol çıkarır.
 *
 * Yandex: rehberlik bildirimi özel RemoteViews kullanır ("Arka plan
 * navigasyonu" ayarı açık olmalı); görünüm şişirilip tüm TextView'lar
 * okunur (YaNaviPeb yaklaşımı). Google Maps: extras başlık/metin.
 * Biçim belgelenmemiştir — sezgisel ayrıştırma; [lastDump] ham veriyi
 * gösterir ki kurallar gerçek bildirime göre ayarlanabilsin.
 */
object NavParser {
    const val YANDEX_NAVI = "ru.yandex.yandexnavi"
    const val YANDEX_MAPS = "ru.yandex.yandexmaps"
    const val GMAPS = "com.google.android.apps.maps"
    const val WAZE = "com.waze"
    val PACKAGES = setOf(YANDEX_NAVI, YANDEX_MAPS, GMAPS, WAZE)

    /** Bölgesel Yandex sürümleri farklı paket adı kullanabilir. */
    fun isNavPackage(pkg: String): Boolean =
        pkg in PACKAGES || ("yandex" in pkg && ("navi" in pkg || "maps" in pkg))

    @Volatile
    var lastDump: String = "(henüz navigasyon bildirimi yok)"

    class Raw(val pkg: String, val items: List<Pair<String, String>>) {
        fun dump() = "pkg=$pkg\n" + items.joinToString("\n") { "${it.first} = ${it.second}" }
    }

    fun collect(ctx: Context, sbn: StatusBarNotification): Raw {
        val items = mutableListOf<Pair<String, String>>()
        val n = sbn.notification
        val ex = n.extras
        for (k in listOf(
            Notification.EXTRA_TITLE, Notification.EXTRA_TITLE_BIG, Notification.EXTRA_TEXT,
            Notification.EXTRA_BIG_TEXT, Notification.EXTRA_SUB_TEXT, Notification.EXTRA_INFO_TEXT,
            Notification.EXTRA_SUMMARY_TEXT,
        )) {
            ex.getCharSequence(k)?.toString()?.takeIf { it.isNotBlank() }?.let {
                items += k.removePrefix("android.") to it
            }
        }
        ex.getCharSequenceArray(Notification.EXTRA_TEXT_LINES)?.forEach { items += "textLines" to it.toString() }

        @Suppress("DEPRECATION")
        for (rv in listOfNotNull(n.bigContentView, n.contentView)) {
            try {
                val v = rv.apply(ctx, FrameLayout(ctx))
                walk(v, items)
                break   // biri yeterli (aynı içerik)
            } catch (e: Exception) {
                items += "rv_error" to e.javaClass.simpleName
            }
        }
        return Raw(sbn.packageName, items)
    }

    private fun walk(v: View, out: MutableList<Pair<String, String>>) {
        if (v is TextView) {
            val t = v.text?.toString()
            if (!t.isNullOrBlank()) out += idName(v) to t.trim()
        }
        if (v is ViewGroup) for (i in 0 until v.childCount) walk(v.getChildAt(i), out)
    }

    private fun idName(v: View): String =
        if (v.id == View.NO_ID) "view" else try {
            v.resources.getResourceEntryName(v.id)
        } catch (e: Exception) {
            "#${v.id}"
        }

    // --- ayrıştırma ---------------------------------------------------------

    private val DIST = Regex("""(\d+(?:[.,]\d+)?)\s*(km|км|m|м)(?![\p{L}])""", RegexOption.IGNORE_CASE)
    private val TIME = Regex("""(?<!\d)([01]?\d|2[0-3]):([0-5]\d)(?!\d)""")

    private val ROAD_SUFFIX = Regex(
        """([\p{Lu}\d][\p{L}\d.'\- ]{1,40}?\s(?:Caddesi|Cad\.|Cd\.|Sokak|Sokağı|Sk\.|Bulvarı|Blv\.|Bulv\.|Yolu|Otoyolu|Meydanı|Köprüsü|Street|St\.|Avenue|Ave\.|Road|Rd\.|Highway))"""
    )
    private val ROAD_PREFIX = Regex("""((?:улица|ул\.|проспект|просп\.|шоссе|бульвар|переулок|набережная)\s[\p{L}\d\- ]{2,30})""")

    /** Sıra önemli: özelden genele. */
    private val MANEUVERS = listOf(
        "U" to listOf("разворот", "u dönüş", "u-dönüş", "u-turn", "u turn", "geri dön"),
        "RB" to listOf("кругов", "кольц", "dönel kavşa", "göbek", "roundabout", "rotary"),
        "A" to listOf("вы приехали", "финиш", "конец маршрута", "hedefinize", "ulaştınız", "vardınız",
            "varış noktası", "you have arrived", "arrive at", "destination on"),
        "SL" to listOf("левее", "плавно нал", "slight left", "keep left", "hafif sol", "soldan devam", "solda kal"),
        "SR" to listOf("правее", "плавно нап", "slight right", "keep right", "hafif sağ", "sağdan devam", "sağda kal"),
        "L" to listOf("налево", "sola", "turn left", "left"),
        "R" to listOf("направо", "sağa", "turn right", "right"),
        "S" to listOf("прямо", "düz", "straight", "continue", "devam"),
    )

    private fun toMeters(m: MatchResult): Int {
        val v = m.groupValues[1].replace(',', '.').toDoubleOrNull() ?: return 0
        val unit = m.groupValues[2].lowercase(Locale.ROOT)
        return if (unit == "km" || unit == "км") (v * 1000).toInt() else v.toInt()
    }

    private val NOISE = listOf("yandex", "navigator", "navigasyon", "haritalar", "maps", "google",
        " dk", " min", "мин", " sa ", "kalan", "varış", "осталось")
    private val ROAD_IDS = listOf("street", "road", "title", "name", "next", "description", "subtitle")

    /** Ek (Cd./Sk.) yoksa: mesafe/saat/manevra cümlesi/uygulama adı olmayan metin. */
    private fun pickRoad(texts: List<Pair<String, String>>): String? {
        val cands = texts.filter { (_, t) ->
            val l = t.lowercase(Locale.ROOT)
            t.length in 3..80 && t.count { it.isLetter() } >= 3 &&
                DIST.find(t) == null && TIME.find(t) == null &&
                NOISE.none { it in " $l " } &&
                // "Sağa dönün" gibi kısa manevra cümleleri yol değildir
                !(classify(t) != null && t.split(' ').size <= 3)
        }
        return (cands.firstOrNull { (id, _) -> ROAD_IDS.any { it in id.lowercase(Locale.ROOT) } }
            ?: cands.firstOrNull())?.second?.trim()
    }

    private val ALERTS = listOf(
        "cam" to listOf("radar", "kamera", "hız kontrol", "камер", "speed camera", "speed trap", "police"),
        "hazard" to listOf("kaza", "yol çalışma", "tehlike", "dikkat", "yolda araç", "авари", "ремонт",
            "accident", "hazard", "road work", "construction"),
        "traffic" to listOf("trafik", "yoğun", "sıkışık", "tıkan", "пробк", "traffic", "jam"),
    )

    /** Bildirimde radar / tehlike / trafik geçiyorsa (tür, satır, mesafe). */
    fun alertFrom(raw: Raw): Triple<String, String, Int?>? {
        for ((kind, keys) in ALERTS) {
            for ((id, t) in raw.items) {
                if (id.endsWith("error")) continue
                val l = t.lowercase(Locale.ROOT)
                if (keys.any { it in l }) return Triple(kind, t.take(44), DIST.find(t)?.let { toMeters(it) })
            }
        }
        return null
    }

    fun classify(text: String): String? {
        val t = text.lowercase(Locale.ROOT)
        for ((code, keys) in MANEUVERS) if (keys.any { it in t }) return code
        return null
    }

    fun parse(raw: Raw): NavData? {
        val texts = raw.items.filter { !it.first.endsWith("error") }
        if (texts.isEmpty()) return null

        var dist: Int? = null
        var remain: Int? = null
        var eta: Int? = null
        for ((id, t) in texts) {
            val idl = id.lowercase(Locale.ROOT)
            DIST.find(t)?.let { m ->
                val d = toMeters(m)
                when {
                    "remain" in idl || "total" in idl -> if (remain == null) remain = d
                    dist == null -> dist = d
                    remain == null -> remain = d
                }
            }
            if (eta == null) TIME.find(t)?.let { eta = it.groupValues[1].toInt() * 60 + it.groupValues[2].toInt() }
        }

        val man = classify(texts.joinToString(" | ") { it.second })
        if (dist == null && man == null) return null

        var road: String? = null
        for ((_, t) in texts) {
            road = (ROAD_SUFFIX.find(t) ?: ROAD_PREFIX.find(t))?.groupValues?.get(1)?.trim()
            if (road != null) break
        }
        if (road == null) road = pickRoad(texts)

        val src = when (raw.pkg) { GMAPS -> "gmaps"; WAZE -> "waze"; else -> "yandex" }
        return NavData(man, dist, remain, eta, road?.take(60), src)
    }
}
