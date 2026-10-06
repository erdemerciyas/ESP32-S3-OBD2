package com.obdnav.bridge

import android.Manifest
import android.app.Activity
import android.content.ClipData
import android.content.ClipboardManager
import android.content.ComponentName
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Typeface
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.view.Gravity
import android.view.View
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast

/** Ana ekran: canlı durum, kurulum, işlemler. Asıl iş NavListenerService + TripService + BleLink'te. */
class MainActivity : Activity() {
    private val main = Handler(Looper.getMainLooper())

    private lateinit var heroCard: LinearLayout
    private lateinit var heroDot: View
    private lateinit var heroTitle: TextView
    private lateinit var heroSub: TextView
    private lateinit var navLine: TextView
    private lateinit var navSub: TextView
    private lateinit var gpsVal: TextView
    private lateinit var gpsSub: TextView
    private lateinit var radarVal: TextView
    private lateinit var mapVal: TextView
    private lateinit var mapSub: TextView
    private lateinit var tripVal: TextView
    private lateinit var tripSub: TextView
    private lateinit var rollDot: View
    private lateinit var rollSub: TextView
    private lateinit var rollMode: TextView
    private lateinit var rollGps: TextView
    private lateinit var rollSpeed: TextView
    private lateinit var rollSync: TextView
    private lateinit var rollTel: TextView
    private lateinit var rollFile: TextView
    private lateinit var setupCard: LinearLayout
    private lateinit var setupNotif: View
    private lateinit var setupPerm: View
    private lateinit var setupEsp: View
    private lateinit var bridgeBtn: TextView
    private lateinit var log: TextView
    private var demoStep = -1

    private val refresher = object : Runnable {
        override fun run() {
            refresh()
            main.postDelayed(this, 500)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        BleLink.init(this)
        Roll.init(this)
        Ui.applyWindow(this)

        val col = Ui.column(this, 20)
        col.addView(header(), Ui.lp(this, bottomDp = 20))
        col.addView(hero(), Ui.lp(this, bottomDp = 12))
        col.addView(stats(), Ui.lp(this, bottomDp = 12))
        col.addView(rollCard(), Ui.lp(this, bottomDp = 12))
        col.addView(setup(), Ui.lp(this, bottomDp = 12))
        col.addView(actions(), Ui.lp(this, bottomDp = 12))
        col.addView(tools(), Ui.lp(this, bottomDp = 12))
        col.addView(logCard(), Ui.lp(this, bottomDp = 24))
        col.addView(footer(), Ui.lp(this, bottomDp = 12))
        setContentView(ScrollView(this).apply { setBackgroundColor(Ui.BG); addView(col) })

        if (BleLink.hasPermission(this)) BleLink.start()
        if (locationGranted() && !TripService.running) TripService.start(this)
    }

    // --- bölümler --------------------------------------------------------------

    private fun header(): View = Ui.row(this).apply {
        addView(ImageView(this@MainActivity).apply { setImageResource(R.drawable.ic_logo) },
            LinearLayout.LayoutParams(Ui.dp(context, 48), Ui.dp(context, 48)).apply { marginEnd = Ui.dp(context, 14) })
        addView(Ui.column(context).apply {
            addView(Ui.text(context, "AURA Köprü", 22f, Ui.TEXT, true))
            addView(Ui.text(context, "Akıllı araç sistemi · telefon köprüsü", 13f, Ui.DIM))
        }, Ui.lp(context, 0, weight = 1f))
        addView(Ui.text(context, "v${version()}", 12f, Ui.PRIMARY, true).apply {
            background = Ui.rounded(context, Ui.SURFACE_HI, 10, Ui.BORDER)
            setPadding(Ui.dp(context, 10), Ui.dp(context, 4), Ui.dp(context, 10), Ui.dp(context, 4))
        })
    }

    private fun hero(): View {
        heroCard = Ui.card(this)
        heroDot = Ui.dot(this, Ui.DIM, 14)
        heroTitle = Ui.text(this, "", 20f, Ui.TEXT, true)
        heroCard.addView(Ui.row(this).apply { addView(heroDot); addView(heroTitle) })
        heroSub = Ui.text(this, "", 13f, Ui.DIM).apply { setPadding(Ui.dp(context, 24), Ui.dp(context, 2), 0, 0) }
        heroCard.addView(heroSub)
        heroCard.addView(View(this).apply { setBackgroundColor(Ui.BORDER) },
            LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 1).apply {
                topMargin = Ui.dp(this@MainActivity, 14); bottomMargin = Ui.dp(this@MainActivity, 12)
            })
        heroCard.addView(Ui.section(this, "Navigasyon").apply { setPadding(0, 0, 0, Ui.dp(context, 4)) })
        navLine = Ui.text(this, "", 18f, Ui.TEXT, true)
        navSub = Ui.text(this, "", 13f, Ui.DIM)
        heroCard.addView(navLine)
        heroCard.addView(navSub)
        return heroCard
    }

    private fun tile(label: String): Triple<LinearLayout, TextView, TextView> {
        val c = Ui.card(this).apply { setPadding(Ui.dp(context, 14), Ui.dp(context, 12), Ui.dp(context, 14), Ui.dp(context, 12)) }
        c.addView(Ui.text(this, label.uppercase(java.util.Locale("tr")), 11f, Ui.DIM, true).apply { letterSpacing = 0.1f })
        val v = Ui.text(this, "—", 20f, Ui.TEXT, true).apply { setPadding(0, Ui.dp(context, 6), 0, Ui.dp(context, 2)) }
        val s = Ui.text(this, "", 12f, Ui.DIM).apply { maxLines = 2 }
        c.addView(v)
        c.addView(s)
        return Triple(c, v, s)
    }

    private fun stats(): View {
        val grid = Ui.column(this)
        val gps = tile("GPS"); gpsVal = gps.second; gpsSub = gps.third
        val radar = tile("Radar"); radarVal = radar.second
        radar.third.text = "OSM hız kameraları"
        val map = tile("Harita"); mapVal = map.second; mapSub = map.third
        val trip = tile("Sürüş"); tripVal = trip.second; tripSub = trip.third
        for ((a, b) in listOf(gps to radar, map to trip)) {
            grid.addView(Ui.row(this).apply {
                addView(a.first, Ui.lp(context, 0, weight = 1f).apply { marginEnd = Ui.dp(context, 6) })
                addView(b.first, Ui.lp(context, 0, weight = 1f).apply { marginStart = Ui.dp(context, 6) })
            }, Ui.lp(this, bottomDp = 12))
        }
        return grid
    }

    /** ROLL (performans zamanlayıcı) ölçüm durumu: GNSS hızı, senkron, telemetri, kayıt. */
    private fun rollCard(): View = Ui.card(this).apply {
        rollDot = Ui.dot(context, Ui.DIM)
        addView(Ui.row(context).apply {
            addView(rollDot)
            addView(Ui.text(context, "ROLL ölçümü", 17f, Ui.TEXT, true))
        })
        rollSub = Ui.text(context, "", 13f, Ui.DIM).apply { setPadding(Ui.dp(context, 20), Ui.dp(context, 2), 0, Ui.dp(context, 8)) }
        addView(rollSub)
        rollMode = kv("ESP modu")
        rollGps = kv("GPS")
        rollSpeed = kv("Hız")
        rollSync = kv("Senkron")
        rollTel = kv("Telemetri")
        rollFile = kv("Kayıt")
        addView(Ui.row(context).apply {
            addView(Ui.button(context, "ROLL ayarları", Ui.Style.PRIMARY) {
                startActivity(Intent(context, RollSettingsActivity::class.java))
            }, Ui.lp(context, 0, weight = 1f).apply { marginEnd = Ui.dp(context, 6) })
            addView(Ui.button(context, "ROLL kayıtları", Ui.Style.SECONDARY) {
                startActivity(Intent(context, RollLogActivity::class.java))
            }, Ui.lp(context, 0, weight = 1f).apply { marginStart = Ui.dp(context, 6) })
        }, Ui.lp(context).apply { topMargin = Ui.dp(context, 10) })
    }

    private fun LinearLayout.kv(label: String): TextView {
        val v = Ui.text(context, "—", 14f, Ui.TEXT).apply { gravity = Gravity.END }
        addView(Ui.row(context).apply {
            setPadding(0, Ui.dp(context, 3), 0, Ui.dp(context, 3))
            addView(Ui.text(context, label, 13f, Ui.DIM), Ui.lp(context, 0, weight = 1f))
            addView(v)
        })
        return v
    }

    private fun setupRow(label: String, action: String, onClick: () -> Unit): View {
        val d = Ui.dot(this, Ui.WARN)
        setupCard.addView(Ui.row(this).apply {
            setPadding(0, Ui.dp(context, 6), 0, Ui.dp(context, 6))
            addView(d)
            addView(Ui.text(context, label, 15f), Ui.lp(context, 0, weight = 1f))
            addView(Ui.button(context, action, Ui.Style.GHOST, onClick))
        })
        return d
    }

    private fun setup(): View {
        setupCard = Ui.card(this, Ui.WARN)
        setupCard.addView(Ui.text(this, "Kurulum", 17f, Ui.TEXT, true))
        setupCard.addView(Ui.text(this, "Köprünün çalışması için aşağıdakileri tamamlayın.", 13f, Ui.DIM)
            .apply { setPadding(0, Ui.dp(context, 2), 0, Ui.dp(context, 8)) })
        setupNotif = setupRow("Bildirim erişimi", "Aç") {
            startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS))
        }
        setupPerm = setupRow("Bluetooth · konum · bildirim izni", "Ver") { requestPermissions() }
        setupEsp = setupRow("ESP eşleşmesi (cihaz NAV modunda)", "Bul") { BleLink.scan() }
        return setupCard
    }

    private fun actions(): View = Ui.column(this).apply {
        addView(Ui.section(context, "İşlemler"))
        addView(Ui.button(context, "Sürüş geçmişi", Ui.Style.PRIMARY) {
            startActivity(Intent(context, TripListActivity::class.java))
        }, Ui.lp(context, bottomDp = 10))
        addView(Ui.row(context).apply {
            bridgeBtn = Ui.button(context, "", Ui.Style.SECONDARY) {
                if (TripService.running) TripService.stop(context) else TripService.start(context)
            }
            addView(bridgeBtn, Ui.lp(context, 0, weight = 1f).apply { marginEnd = Ui.dp(context, 6) })
            addView(Ui.button(context, "ESP'yi yeniden ara", Ui.Style.SECONDARY) { BleLink.scan() },
                Ui.lp(context, 0, weight = 1f).apply { marginStart = Ui.dp(context, 6) })
        })
    }

    private fun tools(): View = Ui.card(this).apply {
        addView(Ui.text(context, "Araçlar", 17f, Ui.TEXT, true))
        addView(Ui.text(context, "Test ve sorun giderme", 13f, Ui.DIM).apply { setPadding(0, 0, 0, Ui.dp(context, 6)) })
        val left = Gravity.START or Gravity.CENTER_VERTICAL
        addView(Ui.button(context, "Demo rota gönder", Ui.Style.GHOST) { startDemo() }.apply { gravity = left })
        addView(Ui.button(context, "Son navigasyon bildirimini kopyala", Ui.Style.GHOST) {
            val cm = getSystemService(CLIPBOARD_SERVICE) as ClipboardManager
            cm.setPrimaryClip(ClipData.newPlainText("nav-dump", NavParser.lastDump))
            Toast.makeText(context, "Kopyalandı", Toast.LENGTH_SHORT).show()
        }.apply { gravity = left })
        addView(Ui.button(context, "Kayıtlı ESP'yi unut", Ui.Style.GHOST) { BleLink.forget() }.apply { gravity = left })
    }

    private fun logCard(): View = Ui.card(this).apply {
        log = Ui.text(context, "", 11.5f, Ui.DIM).apply {
            typeface = Typeface.MONOSPACE
            visibility = View.GONE
            setPadding(0, Ui.dp(context, 10), 0, 0)
        }
        val toggle = Ui.text(context, "Göster", 13f, Ui.PRIMARY, true)
        addView(Ui.row(context).apply {
            addView(Ui.text(context, "Günlük", 17f, Ui.TEXT, true), Ui.lp(context, 0, weight = 1f))
            addView(toggle)
            isClickable = true
            setOnClickListener {
                val show = log.visibility != View.VISIBLE
                log.visibility = if (show) View.VISIBLE else View.GONE
                toggle.text = if (show) "Gizle" else "Göster"
            }
        })
        addView(log)
    }

    private fun footer(): View = Ui.column(this).apply {
        gravity = Gravity.CENTER_HORIZONTAL
        addView(Ui.text(context, "GELİŞTİREN", 10f, Ui.DIM, true).apply { letterSpacing = 0.2f })
        addView(Ui.text(context, "Erdem ERCİYAS", 16f, Ui.TEXT, true).apply { setPadding(0, Ui.dp(context, 2), 0, Ui.dp(context, 2)) })
        addView(Ui.text(context, "© 2026 · AURA Köprü v${version()} · Harita © OpenStreetMap", 11f, Ui.DIM))
    }

    // --- durum -------------------------------------------------------------------

    override fun onResume() {
        super.onResume()
        BridgeLog.listener = { main.post { log.text = BridgeLog.text() } }
        main.post(refresher)
    }

    override fun onPause() {
        super.onPause()
        BridgeLog.listener = null
        main.removeCallbacks(refresher)
    }

    private fun version(): String = try {
        packageManager.getPackageInfo(packageName, 0).versionName ?: "?"
    } catch (_: Exception) { "?" }

    private fun locationGranted() =
        checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED

    private fun listenerEnabled(): Boolean {
        val flat = Settings.Secure.getString(contentResolver, "enabled_notification_listeners") ?: return false
        return flat.contains(ComponentName(this, NavListenerService::class.java).flattenToString())
    }

    private fun maneuverName(m: String?) = when (m) {
        "R" -> "Sağa dön"; "L" -> "Sola dön"; "SR" -> "Hafif sağa"; "SL" -> "Hafif sola"
        "U" -> "U dönüşü"; "RB" -> "Dönel kavşak"; "A" -> "Varış"; "S" -> "Düz devam"
        else -> "Manevra"
    }

    private fun refresh() {
        val notifOk = listenerEnabled()
        val permOk = BleLink.hasPermission(this) && locationGranted()
        val espKnown = BleLink.savedAddress != null

        val (color, title, sub) = when (BleLink.state) {
            BleLink.State.READY -> Triple(Ui.OK, "ESP bağlı", "${BleLink.savedAddress} · veri akışı aktif")
            BleLink.State.CONNECTING -> Triple(Ui.WARN, "ESP bekleniyor", "Cihazda NAV modunu seçin — bağlantı kendiliğinden kurulur")
            BleLink.State.SCANNING -> Triple(Ui.PRIMARY, "ESP aranıyor…", "AURA yayını bekleniyor")
            BleLink.State.IDLE -> Triple(Ui.CRIT, "Bağlı değil", if (espKnown) "Bluetooth'u açın" else "Kurulumdan ESP'yi bulun")
        }
        Ui.setDot(heroDot, color)
        heroTitle.text = title
        heroSub.text = sub
        heroCard.background = Ui.rounded(this, Ui.SURFACE, 18, if (BleLink.state == BleLink.State.READY) 0x5500E676 else Ui.BORDER)

        val d = NavListenerService.lastData
        if (d != null) {
            navLine.text = "${maneuverName(d.maneuver)}${d.distM?.let { " · $it m" } ?: ""}"
            navSub.text = listOfNotNull(d.road, d.src.replaceFirstChar { it.uppercase() }).joinToString("  ·  ")
        } else {
            navLine.text = "Rota yok"
            navSub.text = "Yandex, Google Haritalar veya Waze'de rota başlatın"
        }

        val fix = TripService.lastFix
        when {
            !TripService.running -> { gpsVal.text = "Kapalı"; gpsSub.text = "Köprüyü başlatın" }
            fix == null -> { gpsVal.text = "Aranıyor"; gpsSub.text = "Konum bekleniyor" }
            else -> {
                gpsVal.text = "${if (fix.hasSpeed()) (fix.speed * 3.6f).toInt() else 0} km/h"
                gpsSub.text = "${(System.currentTimeMillis() - fix.time) / 1000} sn önce · ±${fix.accuracy.toInt()} m"
            }
        }
        radarVal.text = "${Radar.count}"
        val mi = MapFeeder.lastInfo
        mapVal.text = when {
            mi == "-" -> "—"
            "ESP'de" in mi -> "Gösteriliyor"
            "reddetti" in mi -> "Hata"
            else -> "Gönderiliyor"
        }
        mapSub.text = if (mi == "-") "Konum gelince gönderilir" else "${MapFeeder.modeText} · $mi"
        val cur = TripRecorder.current
        if (cur != null) {
            tripVal.text = "%.1f km".format(cur.distM / 1000)
            tripSub.text = "Kayıt sürüyor · ${cur.durationS / 60} dk"
        } else {
            tripVal.text = "${TripRecorder.list(this).size}"
            tripSub.text = "kayıtlı sürüş"
        }

        refreshRoll()

        Ui.setDot(setupNotif, if (notifOk) Ui.OK else Ui.WARN)
        Ui.setDot(setupPerm, if (permOk) Ui.OK else Ui.WARN)
        Ui.setDot(setupEsp, if (espKnown) Ui.OK else Ui.WARN)
        setupCard.visibility = if (notifOk && permOk && espKnown) View.GONE else View.VISIBLE
        bridgeBtn.text = if (TripService.running) "Köprü: Açık" else "Köprü: Kapalı"
        bridgeBtn.setTextColor(if (TripService.running) Ui.OK else Ui.DIM)
        if (log.visibility == View.VISIBLE) log.text = BridgeLog.text()
    }

    private fun refreshRoll() {
        val mode = Roll.espMode
        val (color, sub) = when {
            Roll.active -> Ui.OK to "Ölçüm ve kayıt sürüyor"
            BleLink.state != BleLink.State.READY -> Ui.DIM to "ESP bağlı değil"
            mode == "roll" && !TripService.running -> Ui.WARN to "ROLL için köprüyü başlat"
            mode == "roll" -> Ui.WARN to "Başlatılıyor…"
            else -> Ui.DIM to "Cihazda ROLL modunu seçin"
        }
        Ui.setDot(rollDot, color)
        rollSub.text = sub
        rollMode.text = if (mode.isNullOrEmpty()) "—" else mode.uppercase(java.util.Locale.US)
        rollGps.text = if (Roll.active)
            "%.1f Hz · %s uydu".format(Roll.gpsRate.rate(), if (Roll.sats >= 0) "${Roll.sats}" else "—") else "—"
        rollSpeed.text = when {
            Roll.lastSpeed < 0 -> "—"
            Roll.lastSa >= 0 -> "%.1f km/h · ±%.2f m/s".format(Roll.lastSpeed * 3.6f, Roll.lastSa)
            else -> "%.1f km/h".format(Roll.lastSpeed * 3.6f)
        }
        rollSync.text = when {
            Roll.syncValid -> "ofset ${Roll.offsetUs / 1000} ms · rtt %.1f ms".format(Roll.rttUs / 1000f)
            mode == "roll" -> "bekleniyor (${Roll.syncGood}/3)"
            else -> "—"
        }
        val imu = Roll.imuRate.rate()
        val obd = Roll.obdRate.rate()
        rollTel.text = if (imu > 0 || obd > 0)
            "IMU %.0f/s · OBD %.1f/s%s".format(imu, obd, if (Roll.lastObdKmh >= 0) " · ${Roll.lastObdKmh} km/h" else "") else "—"
        rollFile.text = Roll.file?.let { "${it.name} · ${Roll.sizeText(Roll.fileBytes)}" } ?: "—"
    }

    private fun requestPermissions() {
        val perms = mutableListOf(Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION)
        if (Build.VERSION.SDK_INT >= 31) {
            perms += Manifest.permission.BLUETOOTH_SCAN
            perms += Manifest.permission.BLUETOOTH_CONNECT
        }
        if (Build.VERSION.SDK_INT >= 33) perms += Manifest.permission.POST_NOTIFICATIONS
        requestPermissions(perms.toTypedArray(), 1)
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (BleLink.hasPermission(this)) BleLink.start()
        if (locationGranted() && !TripService.running) TripService.start(this)
        refresh()
    }

    // --- demo: telefon navigasyonu olmadan ESP ekranını dener -------------------

    private val demo = listOf(
        NavData("R", 300, 4200, 18 * 60 + 42, "Atatürk Bulvarı", "demo"),
        NavData("SL", 450, 3800, 18 * 60 + 42, "Gazi Mustafa Kemal Blv.", "demo"),
        NavData("RB", 600, 3300, 18 * 60 + 41, "Kızılay Meydanı", "demo"),
        NavData("L", 250, 2600, 18 * 60 + 40, "Ziya Gökalp Caddesi", "demo"),
        NavData("U", 120, 1500, 18 * 60 + 39, "Ziya Gökalp Caddesi", "demo"),
        NavData("A", 80, 80, 18 * 60 + 38, "Hedef", "demo"),
    )

    private fun startDemo() {
        if (BleLink.state != BleLink.State.READY) {
            Toast.makeText(this, "Önce ESP'ye bağlanın", Toast.LENGTH_SHORT).show()
            return
        }
        demoStep = 0
        BleLink.send(Proto.start())
        BridgeLog.add("Demo başladı")
        main.post(demoTick)
    }

    private val demoTick = object : Runnable {
        var sub = 0
        override fun run() {
            if (demoStep < 0) return
            if (demoStep >= demo.size) {
                BleLink.send(Proto.stop())
                BridgeLog.add("Demo bitti")
                demoStep = -1
                return
            }
            val d = demo[demoStep]
            val step = d.copy(distM = (d.distM ?: 0) - sub * (d.distM ?: 0) / 5)
            BleLink.send(Proto.update(demoStep * 10 + sub, step, 50))
            if (++sub >= 5) { sub = 0; demoStep++ }
            main.postDelayed(this, 1000)
        }
    }
}
