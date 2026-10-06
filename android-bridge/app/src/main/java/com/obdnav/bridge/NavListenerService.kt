package com.obdnav.bridge

import android.app.Notification
import android.os.Handler
import android.os.Looper
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification

/**
 * Navigasyon bildirimlerini dinler, ESP'ye iletir. Sistem bu servise bağlı
 * kaldığı sürece (bildirim erişimi açık) uygulama arka planda yaşar.
 */
class NavListenerService : NotificationListenerService() {
    private val main = Handler(Looper.getMainLooper())
    private var activeKey: String? = null
    private var seq = 0
    private val seenPackages = HashSet<String>()
    private val lastVerdict = HashMap<String, String>()
    private var lastAlertText: String? = null

    /** Mesaj akmasa da ESP "veri eski" demesin; bağlantı canlı tutulur. */
    private val pinger = object : Runnable {
        override fun run() {
            if (BleLink.state == BleLink.State.READY) BleLink.send(Proto.ping(++seq))
            main.postDelayed(this, PING_MS)
        }
    }

    override fun onListenerConnected() {
        instance = this
        BleLink.init(this)
        BridgeLog.add("Bildirim dinleyici aktif")
        BleLink.start()
        main.removeCallbacks(pinger)
        main.postDelayed(pinger, PING_MS)
        try {
            activeNotifications?.filter { NavParser.isNavPackage(it.packageName) }?.forEach { handle(it) }
        } catch (_: Exception) { }
    }

    override fun onListenerDisconnected() {
        instance = null
        main.removeCallbacks(pinger)
    }

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        if (seenPackages.add(sbn.packageName)) BridgeLog.add("Bildirim kaynağı: ${sbn.packageName}")
        if (NavParser.isNavPackage(sbn.packageName)) handle(sbn)
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification) {
        if (sbn.key == activeKey) {
            activeKey = null
            lastData = null
            BleLink.send(Proto.stop())
            BridgeLog.add("Navigasyon bitti")
            TripRecorder.stop(this)
        }
    }

    /** Radar / trafik / tehlike satırı: yalnızca metin değişince gönderilir (ESP 8 sn gösterir). */
    private fun handleAlert(raw: NavParser.Raw) {
        val a = NavParser.alertFrom(raw) ?: return
        if (a.second == lastAlertText) return
        lastAlertText = a.second
        BleLink.send(Proto.alert(a.first, a.second, a.third))
        TripRecorder.addAlert(a.first, a.second)
        BridgeLog.add("Uyarı (${a.first}): ${a.second}")
    }

    private fun handle(sbn: StatusBarNotification) {
        val ongoing = sbn.notification.flags and Notification.FLAG_ONGOING_EVENT != 0
        val raw = NavParser.collect(this, sbn)
        handleAlert(raw)
        val d = NavParser.parse(raw)
        // Kalıcı olmayan bildirim (reklam, ipucu...) yalnızca mesafe içeriyorsa kabul
        val usable = d != null && (ongoing || d.distM != null)
        NavParser.lastDump = "ongoing=$ongoing  ayrıştırma=${if (usable) "OK" else "YOK"}\n${raw.dump()}"
        val verdict = "${sbn.key}|$usable"
        if (lastVerdict[sbn.key] != verdict) {
            lastVerdict[sbn.key] = verdict
            BridgeLog.add("${sbn.packageName}: ${raw.items.size} metin, kalıcı=$ongoing, " +
                if (usable) "okundu" else "navigasyon verisi bulunamadı")
        }
        if (!usable) return
        d!!
        if (activeKey != sbn.key) {
            activeKey = sbn.key
            BleLink.send(Proto.start())
            BridgeLog.add("Navigasyon algılandı (${d.src})")
            TripRecorder.start(d.src)
            if (!TripService.running) TripService.start(this)   // arka planda reddedilebilir
        }
        TripRecorder.addRoad(d.road)
        lastData = d
        val msg = Proto.update(++seq, d)
        lastSent = msg
        BleLink.send(msg)
    }

    companion object {
        private const val PING_MS = 2000L

        @Volatile
        var instance: NavListenerService? = null

        @Volatile
        var lastData: NavData? = null

        @Volatile
        var lastSent: String = "-"

        /** BLE yeniden bağlanınca süren rotayı hemen gönder. */
        fun onLinkReady() {
            lastData?.let {
                BleLink.send(Proto.start())
                BleLink.send(Proto.update(0, it))
            }
        }
    }
}
