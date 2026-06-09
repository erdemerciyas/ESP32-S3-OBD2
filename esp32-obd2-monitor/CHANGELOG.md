# Changelog

Bu proje için anlamlı değişiklikler bu dosyada özetlenir.

Biçim [Keep a Changelog](https://keepachangelog.com/tr/1.1.0/) esas alınır; sürüm numaralandırması henüz semver ile sıkı bağlı değildir.

---

## [Unreleased] — 2026-06-09 oturumu

Waveshare **ESP32-S3-Touch-LCD-2.1** + **BLE ELM327 (OBDBLE)** için stabilite, bağlantı ve UI iyileştirmeleri. Firmware birçok kez **COM3** üzerinden yüklendi (~1,33 MB).

### Başlangıç sorunları

- Ana ekranda BLE ikonu kapalı; ayarlarda bağlı görünmesi
- Reboot sonrası otomatik yeniden bağlanma yok
- Açılışta, Scan/Auto sırasında ve beklemede donma / reset
- Bluetooth ayarlarında Scan ve Auto → cihaz reset
- Scan sırasında ekran titremesi (tarama bitince durması)

### Düzeltildi

#### BLE bağlantı ve FSM

- GATT olaylarından `bt_on_serial_ready()` / `bt_on_serial_lost()` ile connectivity FSM senkronu
- Arka plan auto-connect için `bt_try_connect_saved_async()` (kuyruk tabanlı, bloklamayan)
- OBD probe UI worker yolundan çıkarıldı → ayrı `obd_probe` görevi (PSRAM stack)
- Turuncu BLE ikonu yalnızca gerçek auto-connect denemesinde (`connectivity_auto_active`)

#### Boot ve bellek

- `conn_reconnect` görevi: PSRAM 8 KB stack, gecikme (sonra tamamen kaldırıldı)
- `obd_diagnostic_task` kaldırıldı
- `gauge_update`: stack 12 KB, yenileme **15 Hz** (önce 25 Hz / 12 KB)
- BT UI worker: core 1, internal **8 KB** stack

#### Scan stabilitesi

- `bt_signal_cancel()` — LVGL thread'de bloklamayan BLE iptali
- Tam iptal (`bt_request_cancel_operations`) yalnızca `bt_ui_job` worker'da
- UI scan: **tek geçiş ~10 sn** (çoklu pass yerine)
- INT watchdog: **10 sn** (`CONFIG_ESP_INT_WDT_TIMEOUT_MS`)
- Scan başında BLE hazırlık / iptal sırası düzeltildi

#### Auto butonu

- Eski: kayıtlı MAC + 20 sn tam tarama + 12 cihaza connect denemesi
- Yeni: **yalnızca NVS'teki kayıtlı profile** bağlan (~15 sn)
- Kayıtlı cihaz yoksa: *"No saved device — tap Scan first"*
- `bt_link_up()` artık arka planda full auto-scan yapmıyor

#### Idle crash (hiç dokunmadan reset)

- **`conn_reconnect` arka plan görevi kaldırıldı**
- **`BT_BACKGROUND_AUTO_CONNECT = 0`** (`app.h`) — açılışta arka plan BLE yok
- Boot'ta reset nedeni `conn_log`'a yazılıyor (`int_wdt`, `panic`, `brownout`, vb.)
- LVGL handler'a task watchdog beslemesi

#### UI — otomatik ana ekrana dönüş

- Menü / Ayarlar / Bluetooth / About ekranlarında **30 sn** hareketsizlik → gauge ana ekran
- BLE scan/connect sırasında idle sayacı durur
- Sabit: `DASHBOARD_IDLE_RETURN_MS` (`app.h`)

#### UI — Scan titremesi

- BLE işlemi sırasında gauge arka plan güncellemesi durur
- BLE ikonu yalnızca seviye değişince güncellenir (`ui_conn_ind_apply` dedup)
- **`lvgl_set_rf_quiet(true)`** — scan/connect sırasında LVGL periyodik redraw durur; "Scanning…" mesajı sabit kalır; işlem bitince liste bir kez güncellenir

### Değiştirildi

- `connectivity_bt_ui_begin()` artık LVGL thread'i bloklamaz (`bt_signal_cancel` only)
- `connectivity_maintain_bt()` — arka plan auto-connect kapalıyken yalnızca hafif FSM sync
- Connect progress timer: status label'ları her saniye yenilemez (layout titremesi azaltıldı)

### Bilinçli olarak kapalı / ertelenen

| Özellik | Durum |
|---------|--------|
| Açılışta otomatik BLE reconnect | Kapalı (`BT_BACKGROUND_AUTO_CONNECT=0`) |
| Auto = yakındaki tüm cihazları tara | Kaldırıldı (reset riski) |
| Arka plan OBD probe | Yalnızca manuel bağlantı sonrası |

Açmak için: `BT_BACKGROUND_AUTO_CONNECT=1`, `main.cpp`'de `conn_reconnect` görevini geri eklemek; önce idle stabilite doğrulanmalı.

### Güncel kullanım akışı

1. Açılış → ana gauge ekranı (**otomatik BLE yok**)
2. İlk bağlantı: Menü → Bluetooth → **Scan** (~10 sn) → OBDBLE'ye dokun → profil NVS'e kaydedilir
3. Sonraki seferler: **Auto** veya Scan → cihaz seç
4. Alt ekranda 30 sn bekleme → otomatik ana ekrana dönüş

### Dokunulan ana dosyalar

| Dosya | Değişiklik |
|-------|------------|
| `main/main.cpp` | Reset log, task stack'leri, `conn_reconnect` kaldırıldı |
| `components/connectivity/connectivity.c` | FSM, maintain, UI begin/end, auto mode |
| `components/connectivity/bt_manager.c` | Async connect, UI scan, `bt_signal_cancel` |
| `components/display/ui/bt_settings_ui.c` | Scan/Auto UI, RF quiet tetikleme |
| `components/display/ui/dashboard.c` | Gauge throttle, idle return |
| `components/display/lvgl_port/lvgl_driver.c` | `lvgl_set_rf_quiet()` |
| `components/display/ui/ui_icons.c` | Conn indicator dedup |
| `components/app/app.h` | `DASHBOARD_IDLE_RETURN_MS`, `BT_BACKGROUND_AUTO_CONNECT` |
| `sdkconfig` / `sdkconfig.defaults` | INT WDT 10 sn |

### Flash

```powershell
cd c:\Users\erdem\ESP32\alternative\esp32-obd2-monitor
.\build_flash.ps1 -Action build -Port COM3
.\build_flash.ps1 -Action flash -Port COM3
```

### Test checklist

- [ ] Açılışta 1–2 dk bekleme → reset olmamalı
- [ ] Scan → titreme olmamalı, cihaz listesi gelmeli
- [ ] Cihaz seç → bağlan → ana ekranda gauge + BLE ikonu
- [ ] Auto → kayıtlı OBDBLE (~15 sn, reset yok)
- [ ] Araçta kontak açık → RPM/speed canlı

### Sonraki adımlar (öneri)

1. Seri monitör / `conn_log_dump` ile kalan reset nedenlerini doğrulama
2. Stabilite onayı sonrası kontrollü auto-reconnect (yalnızca kayıtlı MAC, async, uzun aralık)
3. İsteğe bağlı: idle süresi ve scan süresini Settings'e taşıma

---

## [1.0.0] — önceki taban

İlk sürüm: LVGL 9 dashboard, NimBLE central, BLE ELM327, NVS profil, çoklu GATT profili. Ayrıntılar: [`README.md`](README.md), [`UPLOAD.md`](UPLOAD.md).
