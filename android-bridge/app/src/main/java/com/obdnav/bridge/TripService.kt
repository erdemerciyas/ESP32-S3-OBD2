package com.obdnav.bridge

import android.annotation.SuppressLint
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import android.os.Looper

/**
 * Ön plan servisi (konum tipi): GPS'i açık tutar, her konumu ESP'ye (loc),
 * sürüş kaydına, radara ve harita besleyicisine dağıtır. Android arka planda
 * konum için bu kalıcı bildirimi zorunlu tutar. Uygulama ekranından başlatılır.
 */
class TripService : Service(), LocationListener {
    private var lastSent = 0L
    private var lastGps = 0L

    override fun onBind(intent: Intent?): IBinder? = null

    @SuppressLint("MissingPermission")
    override fun onCreate() {
        super.onCreate()
        BleLink.init(this)
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(
            NotificationChannel(CHANNEL, "AURA köprü", NotificationManager.IMPORTANCE_LOW)
        )
        val open = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE
        )
        val n = Notification.Builder(this, CHANNEL)
            .setSmallIcon(R.drawable.ic_stat_nav)
            .setContentTitle("AURA köprü çalışıyor")
            .setContentText("Konum, radar ve harita ESP'ye gönderiliyor")
            .setContentIntent(open)
            .setOngoing(true)
            .build()
        try {
            if (Build.VERSION.SDK_INT >= 29) {
                startForeground(NOTIF_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION)
            } else {
                startForeground(NOTIF_ID, n)
            }
            val lm = getSystemService(Context.LOCATION_SERVICE) as LocationManager
            lm.requestLocationUpdates(LocationManager.GPS_PROVIDER, 1000L, 0f, this, Looper.getMainLooper())
            // Kapalı alanda GPS yokken haritanın yine gelmesi için ağ konumu (yalnız harita merkezi)
            if (lm.isProviderEnabled(LocationManager.NETWORK_PROVIDER)) {
                lm.requestLocationUpdates(LocationManager.NETWORK_PROVIDER, 5000L, 0f, this, Looper.getMainLooper())
            }
            running = true
            BridgeLog.add("GPS açık")
        } catch (e: Exception) {
            BridgeLog.add("Konum servisi başlatılamadı: ${e.javaClass.simpleName} (konum izni?)")
            stopSelf()
        }
        BleLink.start()
    }

    override fun onDestroy() {
        running = false
        (getSystemService(Context.LOCATION_SERVICE) as LocationManager).removeUpdates(this)
        BridgeLog.add("GPS kapalı")
        super.onDestroy()
    }

    override fun onLocationChanged(loc: Location) {
        val now = System.currentTimeMillis()
        val gps = loc.provider == LocationManager.GPS_PROVIDER
        if (gps) lastGps = now
        else if (now - lastGps < 5000) return   // GPS varken ağ konumunu yok say
        lastFix = loc
        MapFeeder.onLocation(this, loc.latitude, loc.longitude)
        // Kaba ağ konumu rota kaydını / radarı / ESP çizgisini bozmasın
        if (!loc.hasAccuracy() || loc.accuracy > 50f) return
        TripRecorder.addPoint(this, loc)
        Radar.check(this, loc)?.let { BleLink.send(it) }
        if (now - lastSent >= 1000) {
            lastSent = now
            BleLink.send(Proto.loc(
                loc.latitude, loc.longitude,
                if (loc.hasSpeed()) (loc.speed * 3.6f + 0.5f).toInt() else null,
                if (loc.hasBearing()) loc.bearing.toInt() else null,
            ))
        }
    }

    @Deprecated("API < 29")
    override fun onStatusChanged(provider: String?, status: Int, extras: Bundle?) { }
    override fun onProviderEnabled(provider: String) { }
    override fun onProviderDisabled(provider: String) { BridgeLog.add("Telefonun konumu kapalı") }

    companion object {
        private const val CHANNEL = "bridge"
        private const val NOTIF_ID = 1

        @Volatile
        var running = false
            private set

        @Volatile
        var lastFix: Location? = null
            private set

        fun start(ctx: Context) {
            try {
                ctx.startForegroundService(Intent(ctx, TripService::class.java))
            } catch (e: Exception) {
                BridgeLog.add("Servis başlatılamadı: ${e.javaClass.simpleName}")
            }
        }

        fun stop(ctx: Context) {
            ctx.stopService(Intent(ctx, TripService::class.java))
        }
    }
}
