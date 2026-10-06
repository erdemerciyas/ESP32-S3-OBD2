package com.obdnav.bridge

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import android.os.SystemClock
import java.util.UUID

/**
 * ESP32 "AURA" BLE bağlantısı (telefon = central). Tüm durum ana
 * iş parçacığında. Kayıtlı adrese autoConnect ile bağlanır: ESP NAV moduna
 * geçip reklam yaptığında bağlantı kendiliğinden kurulur.
 */
@SuppressLint("MissingPermission")
object BleLink {
    val SVC: UUID = UUID.fromString("7c6a0001-2f4b-4b8e-9d3a-5e1f0c2a9b10")
    private val RX: UUID = UUID.fromString("7c6a0002-2f4b-4b8e-9d3a-5e1f0c2a9b10")
    private val TX: UUID = UUID.fromString("7c6a0003-2f4b-4b8e-9d3a-5e1f0c2a9b10")
    private val MAP: UUID = UUID.fromString("7c6a0004-2f4b-4b8e-9d3a-5e1f0c2a9b10")
    private val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    enum class State { IDLE, SCANNING, CONNECTING, READY }

    @Volatile
    var state = State.IDLE
        private set

    private lateinit var app: Context
    private val main = Handler(Looper.getMainLooper())
    private var gatt: BluetoothGatt? = null
    private var rx: BluetoothGattCharacteristic? = null
    private var mapChr: BluetoothGattCharacteristic? = null
    private val queue = ArrayDeque<ByteArray>()
    private val bulk = ArrayDeque<ByteArray>()      // harita parçaları (MAP, yanıtsız)
    private var mtu = 23

    /** Harita aktarımı sürüyor mu (yenisini başlatmadan önce bakılır). */
    val mapBusy: Boolean get() = bulk.isNotEmpty()
    private var writing = false
    private var writeStart = 0L
    private var scanCb: ScanCallback? = null

    fun init(ctx: Context) {
        if (!::app.isInitialized) app = ctx.applicationContext
    }

    private fun prefs() = app.getSharedPreferences("bridge", Context.MODE_PRIVATE)
    val savedAddress: String? get() = if (::app.isInitialized) prefs().getString("mac", null) else null

    private fun adapter(): BluetoothAdapter? =
        (app.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter

    fun hasPermission(ctx: Context): Boolean {
        val perms = if (Build.VERSION.SDK_INT >= 31)
            listOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
        else listOf(Manifest.permission.ACCESS_FINE_LOCATION)
        return perms.all { ctx.checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED }
    }

    private fun ready(): BluetoothAdapter? {
        if (!hasPermission(app)) { BridgeLog.add("Bluetooth izni yok"); return null }
        val a = adapter()
        if (a == null || !a.isEnabled) { BridgeLog.add("Telefonun Bluetooth'u kapalı"); return null }
        return a
    }

    /** Kayıtlı ESP varsa ona bağlan, yoksa tara. */
    fun start() = main.post {
        if (gatt != null || state == State.SCANNING) return@post
        val a = ready() ?: return@post
        val mac = savedAddress
        if (mac == null) doScan(a) else connect(a.getRemoteDevice(mac))
    }

    /** Elle tarama: kayıtlı adresi yenisiyle değiştirir. */
    fun scan() = main.post {
        val a = ready() ?: return@post
        closeGatt()
        doScan(a)
    }

    fun forget() = main.post {
        stopScan()
        closeGatt()
        state = State.IDLE
        prefs().edit().remove("mac").apply()
        BridgeLog.add("Kayıtlı ESP silindi")
    }

    private fun doScan(a: BluetoothAdapter) {
        val scanner = a.bluetoothLeScanner ?: return
        stopScan()
        state = State.SCANNING
        val cb = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                main.post {
                    if (state != State.SCANNING) return@post
                    stopScan()
                    prefs().edit().putString("mac", result.device.address).apply()
                    BridgeLog.add("ESP bulundu: ${result.device.address}")
                    connect(result.device)
                }
            }

            override fun onScanFailed(errorCode: Int) {
                main.post { state = State.IDLE; BridgeLog.add("Tarama hatası $errorCode") }
            }
        }
        scanCb = cb
        scanner.startScan(
            listOf(ScanFilter.Builder().setServiceUuid(ParcelUuid(SVC)).build()),
            ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(),
            cb,
        )
        BridgeLog.add("AURA aranıyor... (ESP NAV modunda olmalı)")
        main.postDelayed({
            if (state == State.SCANNING) {
                stopScan()
                state = State.IDLE
                BridgeLog.add("ESP bulunamadı — NAV modunda mı?")
            }
        }, 20_000)
    }

    private fun stopScan() {
        val cb = scanCb ?: return
        scanCb = null
        try { adapter()?.bluetoothLeScanner?.stopScan(cb) } catch (_: Exception) { }
    }

    private fun connect(d: BluetoothDevice) {
        closeGatt()
        state = State.CONNECTING
        BridgeLog.add("ESP bekleniyor: ${d.address}")
        gatt = d.connectGatt(app, true, callback, BluetoothDevice.TRANSPORT_LE)
    }

    private fun closeGatt() {
        gatt?.let { try { it.disconnect(); it.close() } catch (_: Exception) { } }
        gatt = null
        rx = null
        mapChr = null
        queue.clear()
        bulk.clear()
        writing = false
    }

    /** Tek mesaj gönder (sıraya alınır; bağlı değilse atılır). */
    fun send(json: String) = main.post {
        if (state != State.READY) return@post
        if (writing && SystemClock.elapsedRealtime() - writeStart > 2000) writing = false
        while (queue.size >= 8) queue.removeFirst()   // tıkanırsa eskileri at
        queue.addLast(json.toByteArray(Charsets.UTF_8))
        pump()
    }

    /**
     * Harita resmi: başlık (JSON) mesaj kuyruğundan, JPEG parçaları MAP
     * karakteristiğine onaylı yazılır. Parça: [id][offset 3 bayt LE][veri].
     */
    fun sendMap(id: Int, jpeg: ByteArray, lat: Double, lon: Double, zoom: Int, w: Int, h: Int) = main.post {
        if (state != State.READY || mapChr == null) return@post
        bulk.clear()   // yarım kalan eski resim: ESP id uyuşmazlığıyla zaten atar
        queue.addLast(Proto.mapHeader(id, jpeg.size, w, h, lat, lon, zoom).toByteArray())
        val step = (mtu - 3 - 4).coerceIn(16, 240)
        var off = 0
        while (off < jpeg.size) {
            val n = minOf(step, jpeg.size - off)
            val p = ByteArray(4 + n)
            p[0] = id.toByte()
            p[1] = (off and 0xFF).toByte()
            p[2] = ((off shr 8) and 0xFF).toByte()
            p[3] = ((off shr 16) and 0xFF).toByte()
            System.arraycopy(jpeg, off, p, 4, n)
            bulk.addLast(p)
            off += n
        }
        pump()
    }

    private fun pump() {
        val g = gatt ?: return
        if (writing) return
        // Mesajlar önce; harita parçaları araya girer ama mesajı geciktirmez
        val isMsg = queue.isNotEmpty()
        val c = (if (isMsg) rx else mapChr) ?: return
        val data = (if (isMsg) queue.removeFirstOrNull() else bulk.removeFirstOrNull()) ?: return
        writing = true
        writeStart = SystemClock.elapsedRealtime()
        // Harita parçaları da onaylı: ESP tamponu taşıp parça kaybolmasın (resim çöpe gider)
        val type = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        val ok = if (Build.VERSION.SDK_INT >= 33) {
            g.writeCharacteristic(c, data, type) == BluetoothStatusCodes.SUCCESS
        } else {
            c.writeType = type
            @Suppress("DEPRECATION")
            c.value = data
            @Suppress("DEPRECATION")
            g.writeCharacteristic(c)
        }
        if (!ok) {
            writing = false
            if (isMsg) queue.addFirst(data) else bulk.addFirst(data)   // yığın meşgul: az sonra tekrar
            main.postDelayed({ pump() }, 20)
        }
    }

    private fun enableNotify(g: BluetoothGatt, c: BluetoothGattCharacteristic) {
        g.setCharacteristicNotification(c, true)
        val d = c.getDescriptor(CCCD) ?: return onReady()
        if (Build.VERSION.SDK_INT >= 33) {
            g.writeDescriptor(d, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
        } else {
            @Suppress("DEPRECATION")
            d.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            @Suppress("DEPRECATION")
            g.writeDescriptor(d)
        }
    }

    private fun onReady() {
        state = State.READY
        BridgeLog.add("ESP bağlı ve hazır (MTU $mtu)")
        gatt?.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH)   // harita aktarımı için
        queue.addFirst(Proto.hello("android").toByteArray())
        pump()
        NavListenerService.onLinkReady()
        MapFeeder.reset()
    }

    private val callback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            main.post {
                if (g != gatt) return@post
                if (newState == BluetoothProfile.STATE_CONNECTED) {
                    BridgeLog.add("ESP'ye bağlandı")
                    if (!g.requestMtu(247)) g.discoverServices()
                } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                    BridgeLog.add("ESP bağlantısı koptu ($status)")
                    val dev = g.device
                    closeGatt()
                    state = State.CONNECTING
                    main.postDelayed({ if (gatt == null && state == State.CONNECTING) connect(dev) }, 1500)
                }
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            main.post {
                if (g != gatt) return@post
                if (status == BluetoothGatt.GATT_SUCCESS) this@BleLink.mtu = mtu
                g.discoverServices()
            }
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            main.post {
                if (g != gatt) return@post
                val svc = g.getService(SVC)
                val r = svc?.getCharacteristic(RX)
                val t = svc?.getCharacteristic(TX)
                if (r == null || t == null) {
                    BridgeLog.add("ESP'de navigasyon servisi yok")
                    return@post
                }
                rx = r
                mapChr = svc.getCharacteristic(MAP)   // eski ESP firmware'inde yok: harita gönderilmez
                enableNotify(g, t)
            }
        }

        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) {
            main.post { if (g == gatt && state != State.READY) onReady() }
        }

        override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) {
            main.post {
                if (g != gatt) return@post
                writing = false
                pump()
            }
        }

        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) {
            onReply(value)
        }

        @Deprecated("API < 33")
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) {
            @Suppress("DEPRECATION")
            c.value?.let { onReply(it) }
        }
    }

    private fun onReply(value: ByteArray) {
        val s = String(value, Charsets.UTF_8)
        if (s.contains("\"mapreq\"")) {
            try {
                val o = org.json.JSONObject(s)
                main.post { MapFeeder.request(o.optString("mode") == "fit", o.optInt("z")) }
            } catch (_: Exception) { }
            return
        }
        if (s.contains("\"mapack\"")) {
            try {
                val o = org.json.JSONObject(s)
                main.post { MapFeeder.onAck(o.optInt("id"), o.optBoolean("ok"), o.optString("why")) }
            } catch (_: Exception) { }
            return
        }
        if (!s.contains("\"pong\"")) BridgeLog.add("ESP: $s")
    }
}
