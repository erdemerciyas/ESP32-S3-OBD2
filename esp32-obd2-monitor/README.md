# ESP32-S3 OBD2 Araç İzleme Sistemi v1.0.0

Waveshare **[ESP32-S3-Touch-LCD-2.1](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.1)** kartı ile araç OBD2 verilerini gerçek zamanlı izleyen gösterge paneli. 480×480 RGB ekranda LVGL 9 arayüzü; ekran metinleri **İngilizce**.

> **Yükleme, donanım pinleri, sorun giderme ve oturum notları:** [`UPLOAD.md`](UPLOAD.md) — flash/build için birincil kaynak.

## Genel Bakış

### Özellikler

| Alan | Açıklama |
|------|----------|
| **PID'ler** | RPM, speed, coolant, throttle, fuel, load, intake, battery, derived fuel rate (MAF+RPM), DTC |
| **Evrensel OBD** | Bağlantıda `0100` / `0120` / `0140` destek bitmap'i; yalnız desteklenen PID'ler poll edilir; desteklenmeyen göstergeler kaydırmadan çıkar |
| **Gösterge UI** | Tam ekran yay; büyük değer fontu (`font_gauge_96`); sola/sağa yalnızca **mevcut** göstergeler; geçersiz değer `--`; alt nokta `n/N` |
| **Bluetooth HUD** | Sağ üst BT ikonu: kapalı → bağlanıyor → ELM → OBD (renk/parlama) — gösterge, menü, Connection ekranı |
| **Açılış** | `splash.c` ~5 sn EXTREME/MONITOR; ardından ana gösterge |
| **Menü** | Çift dokunma; **Gauge / Connection / Settings / About**; Font Awesome ikonları |
| **Bağlantı** | **BLE ELM327** (NimBLE central): Scan, Auto, Forget, Disconnect; kayıtlı adaptör; USB-UART (GPIO43/44) yedek |
| **Bağlantı günlüğü** | `conn_log` — son 20 olay NVS'te; açılışta seri porta dökülür |
| **FSM** | `DISCONNECTED` → `LINK_UP` → `ELM_INIT` → `OBD_READY` (`elm327_session`) |
| **Telemetri** | `telemetry_snapshot_t` — UI ↔ `obd` gevşek bağlı |
| **NVS** | `schema=5` — BT profili (ad/MAC/addr type), max RPM/speed, gösterge sırası/renkleri, parlaklık, haptic |
| **Tema** | Dark only (Workshop at Dusk); buzzer (TCA9554 EXIO8) |

**Not:** ESP32-S3 **klasik Bluetooth (SPP) desteklemez** — yalnızca **BLE** ELM327 adaptörleri kullanılabilir. Klasik “OBDII” SPP dongle'ları bu kartta çalışmaz.

### Evrensel PID akışı

1. OBD hazır olunca `obd_service_discover_supported_pids()` → ECU bitmap veya tablo probe.
2. `pid_support_should_poll()` ile yalnız desteklenen Mode 01 PID'leri sorgulanır.
3. 3 ardışık hata → `*_valid` sıfırlanır, PID “desteklenmiyor” işaretlenir.
4. `gauge_sync_availability()` → kaydırma ve alt noktalar yalnız desteklenen göstergeler.

Kod: `components/obd/pid_support.c`, `obd_service.c`, `components/display/ui/gauge.c`.

### Desteklenen araçlar

**OBD-II uyumlu** tüm araçlar hedeflenir (PID seti ECU'ya göre değişir). Referans geliştirme: **Chevrolet Kalos 2005** — [`docs/vehicle-kalos-2005.md`](docs/vehicle-kalos-2005.md).

## Donanım

| Bileşen | Açıklama |
|---------|----------|
| Kart | Waveshare ESP32-S3-Touch-LCD-2.1 |
| Ekran | 480×480 ST7701 RGB + 3-wire SPI init |
| Dokunmatik | CST820, I2C `0x15` |
| IO genişletici | TCA9554 `0x20` — LCD RST/CS, TP RST, buzzer |
| OBD | BLE ELM327 adaptör (varsayılan) veya UART TX=43, RX=44 @ 38400 |

Ayrıntı: [`UPLOAD.md` — Donanım](UPLOAD.md#donanım).

## Kurulum ve yükleme

### Gereksinimler

- **ESP-IDF 5.1+** (doğrulama: **5.3.5**)
- Python 3.8+, Ninja, USB (CP210x / CH340 / USB-JTAG)
- **BLE ELM327** adaptör (NUS, FFE0/FFE1 veya FFE2 notify profilleri)

### Windows

```powershell
$env:IDF_TOOLS_PATH = "C:\Espressif"
$env:Path = "C:\Espressif\python_env\idf5.3_py3.11_env\Scripts;" + $env:Path
. C:\Espressif\frameworks\esp-idf-v5.3.5\export.ps1
cd esp32-obd2-monitor
```

```powershell
.\build_flash.ps1 -Action all -Port COM3
# veya
idf.py build flash -p COM3 monitor
```

### Linux / macOS

```bash
. $IDF_PATH/export.sh
cd esp32-obd2-monitor
./build_flash.sh all /dev/ttyUSB0
```

## Dokunmatik kullanım

| Hareket | Sonuç |
|---------|--------|
| Sola / sağa kaydır | Sonraki / önceki **desteklenen** gösterge (sıra: Settings → Swipe order) |
| Uzun basış (4 sn) | Varsayılan açılış göstergesini NVS'e kaydet |
| Alt noktalar + `n/N` | Aktif gösterge (N = bu araçta desteklenen sayı) |
| Çift dokunma | Menü |
| Menü / BACK | Alt sayfalar |

Sabitler: `board_config.h` (`UI_SWIPE_THRESHOLD_PX`, `UI_LONG_PRESS_MS`, `UI_ROUND_INSET`).

## Bluetooth / ELM327 (BLE)

Varsayılan taşıma: **`CONN_TYPE_BLUETOOTH`**. NimBLE central; GATT profilleri: **NUS**, **FFE0/FFE1**, **FFE2** (notify).

### Kullanım akışı

| Adım | ESP32 ekranı |
|------|----------------|
| Adaptör OBD soketine takılı, kontak açık | — |
| Menü → **Connection** → **Scan** | BLE ELM327 adayları listelenir |
| Listeden adaptör seçin | Bağlanır → NVS'e kayıt |
| Başarılı | `Saved: <name> <MAC>` |
| Sonraki açılışlar | Kayıtlı adaptör otomatik denenir |
| **Auto** | Tam BLE taraması + ilk uygun adaptör |
| **Forget** | NVS profilini siler |
| **Disconnect** | Aktif BLE oturumunu kapatır |

**Varsayılan:** `bt_manual_mode=true` — kayıtlı adaptör yokken açılışta otomatik tarama yapılmaz.

NimBLE çağrıları UI thread'den **`bt_cmd` worker** kuyruğu üzerinden yapılır (Scan sırasında çökme önlenir).

### Notlar

- Kontak kapalı: “TCP/serial ready, turn ignition on” benzeri durum (RPM zorunlu değil).
- Timeout: `BT_CONNECT_TIMEOUT_MS` = **15 s** (`app.h`).
- BLE adres tipi (`public` / `random`) NVS'te saklanır (`bt_addr_type`).

## Ayarlar (Settings)

| Öğe | Açıklama |
|-----|----------|
| Brightness | Arka ışık (%0–100) |
| Max RPM / Max speed | Gösterge ölçeği üst sınırı |
| Swipe order | Gösterge kaydırma sırası (+/−) |
| Haptic / Sound | Dokunsal ve ses geri bildirimi |

Tema seçimi kaldırıldı — yalnızca **dark** palet (`styles.c`).

## Proje yapısı

```
esp32-obd2-monitor/
├── main/main.cpp
├── components/
│   ├── app/                  # Sabitler (poll, timeout, gauge count)
│   ├── connectivity/         # FSM, bt_manager, elm327_session
│   │   ├── bt_manager.c      # NimBLE central, bt_cmd worker
│   │   └── bt_elm327_profiles.h
│   ├── display/
│   │   ├── fonts/            # LVGL font .c + gen_fonts.ps1 (fa-brands BT ikonu)
│   │   ├── lvgl_port/
│   │   └── ui/
│   │       ├── dashboard.c, gauge.c, splash.c
│   │       ├── bt_settings_ui.c, ui_icons.c, haptic.c
│   │       └── styles.c
│   ├── obd/
│   │   ├── obd_service.c
│   │   ├── pid_table.c, pid_support.c
│   │   └── vehicle_profile.h
│   ├── telemetry/
│   └── system/               # NVS settings (schema v5), conn_log
├── test/
├── docs/
├── sdkconfig.defaults        # NimBLE, LVGL 128 KB heap, OCT PSRAM
├── build_flash.ps1 / .sh
├── UPLOAD.md
└── README.md
```

## Çalışma modu

| Görev | Öncelik | Stack | Not |
|-------|---------|-------|-----|
| `display_init` | 5 | **40 KB** | LVGL + splash + dashboard |
| `bt_cmd` | 5 | **20 KB** | NimBLE scan/connect/disconnect kuyruğu |
| `conn_reconnect` | 5 | 12 KB | Otomatik yeniden bağlanma |
| `obd_fast` | 6 | 4 KB | 40 ms — desteklenen hızlı PID |
| `obd_slow` | 4 | 4 KB | 2 s — desteklenen yavaş PID |
| `obd_dtc` | 3 | 4 KB | 30 s DTC |
| `gauge_update` | 4 | 8 KB | 25 Hz UI |
| `lvgl_handler` | 5 | 8 KB | core 1 |

Sabitler: `app.h` — `OBD2_FAST_POLL_MS`, `GAUGE_UPDATE_RATE_HZ`, `BT_CONNECT_TIMEOUT_MS`.

## Yapılandırma

`idf.py menuconfig` → **Component config → Bluetooth → NimBLE** (çoğu ayar `sdkconfig.defaults` içinde).

Önemli varsayılanlar:

- `CONFIG_LV_MEM_SIZE_KILOBYTES=128` — dashboard + settings heap (64 KB siyah ekrana yol açabilir)
- `CONFIG_BT_NIMBLE_ENABLED=y`, central role
- `CONFIG_SPIRAM_MODE_OCT=y`

NVS: BT profili, max RPM/speed, gauge order/colors, parlaklık, haptic, varsayılan gösterge.

## Sorun giderme

Ayrıntı: **`UPLOAD.md` → Sorun giderme**.

| Belirti | İlk bakılacak |
|---------|----------------|
| Siyah ekran | LVGL heap 128 KB; lazy settings UI; `display_init` 40 KB stack — UPLOAD.md |
| Scan'de reboot | Eski firmware (NimBLE UI thread); güncel `bt_cmd` worker |
| BT listede var, bağlanmıyor | Adaptör BLE mi? (klasik SPP değil); `conn_log`; 15 s timeout |
| Tüm göstergeler `--` | Kontak; PID keşfi log; ELM327 protokol |
| Kaydırma boş sayfalar | Evrensel PID filtresi; gauge order ayarları |
| Eksik ikon / metin | `gen_fonts.ps1` + `fa-brands-400.ttf` (Bluetooth U+F293) |

## Geliştirme

| Görev | Dosyalar |
|-------|----------|
| Yeni PID + gösterge | `pid_table.c`, `apply_pid` (`obd_service.c`), `gauge_configs[]` |
| Destek bitmap | `pid_support.c` |
| BLE / ELM327 | `bt_manager.c`, `bt_elm327_profiles.h`, `elm327_session.c`, `conn_log.c` |
| UI / ikon | `ui_icons.c`, `dashboard.c`, `bt_settings_ui.c`, `board_config.h` |
| Font üretimi | `components/display/fonts/gen_fonts.ps1`, `font_gauge_96.c` |
| Testler | `test/test_obd_parser.c`, `test/test_pid_support.c` |

## Lisans

MIT License
