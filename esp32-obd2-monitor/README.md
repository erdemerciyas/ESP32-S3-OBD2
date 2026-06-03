# ESP32-S3 OBD2 Araç İzleme Sistemi v1.0.0

Waveshare **[ESP32-S3-Touch-LCD-2.1](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.1)** kartı ile araç OBD2 verilerini gerçek zamanlı izleyen gösterge paneli. 480×480 RGB ekranda LVGL 9 arayüzü.

> **Yükleme, donanım pinleri, sorun giderme ve oturum notları:** [`UPLOAD.md`](UPLOAD.md) — flash/build için birincil kaynak.

## Genel Bakış

### Özellikler

| Alan | Açıklama |
|------|----------|
| **PID’ler** | RPM, hız, antifriz, gaz, yakıt, yük, emme, akü, türetilmiş yakıt tüketimi (MAF+RPM), DTC |
| **Evrensel OBD** | Bağlantıda `0100` / `0120` / `0140` destek bitmap’i; yalnız desteklenen PID’ler poll edilir; desteklenmeyen göstergeler kaydırmadan çıkar |
| **Gösterge UI** | Tam ekran yay; sola/sağa yalnızca **mevcut** göstergeler; geçersiz değer `--`; alt nokta `n/N` (N = araçtaki desteklenen sayı) |
| **WiFi HUD** | Sağ üst `LV_SYMBOL_WIFI`: kapalı → AP → TCP → OBD (renk/parlama) — gösterge, menü, bağlantı ekranı |
| **Açılış** | `splash.c` ~5 sn EXTREME/MONITOR; ardından ana gösterge |
| **Menü** | Çift dokunma; Bağlantı / Ayarlar / Hakkında; Türkçe metin + Font Awesome ikonları |
| **Bağlantı** | WiFi ELM327: **manuel tarama** (varsayılan), kayıtlı profil, gateway TCP keşfi, USB-UART (GPIO43/44) |
| **Bağlantı günlüğü** | `conn_log` — son 20 olay NVS’te; açılışta seri porta dökülür (WiFi/TCP/OBD hata nedeni) |
| **FSM** | `DISCONNECTED` → `LINK_UP` → `ELM_INIT` → `OBD_READY` (`elm327_session`) |
| **Telemetri** | `telemetry_snapshot_t` — UI ↔ `obd` gevşek bağlı |
| **NVS** | `schema=3` — ayarlar, manuel WiFi profili (SSID/şifre/IP:port), varsayılan gösterge |
| **Tema / haptic** | Workshop at Dusk; buzzer (TCA9554 EXIO8) |

**Bluetooth:** ESP32-S3 klasik SPP desteklemez — OBD için WiFi veya USB ELM327.

### Evrensel PID akışı

1. OBD hazır olunca `obd_service_discover_supported_pids()` → ECU bitmap veya tablo probe.
2. `pid_support_should_poll()` ile yalnız desteklenen Mode 01 PID’leri sorgulanır.
3. 3 ardışık hata → `*_valid` sıfırlanır, PID “desteklenmiyor” işaretlenir.
4. `gauge_sync_availability()` → kaydırma ve alt noktalar yalnız desteklenen göstergeler.

Kod: `components/obd/pid_support.c`, `obd_service.c`, `components/display/ui/gauge.c`.

### Desteklenen araçlar

**OBD-II uyumlu** tüm araçlar hedeflenir (PID seti ECU’ya göre değişir). Referans geliştirme: **Chevrolet Kalos 2005** — [`docs/vehicle-kalos-2005.md`](docs/vehicle-kalos-2005.md).

## Donanım

| Bileşen | Açıklama |
|---------|----------|
| Kart | Waveshare ESP32-S3-Touch-LCD-2.1 |
| Ekran | 480×480 ST7701 RGB + 3-wire SPI init |
| Dokunmatik | CST820, I2C `0x15` |
| IO genişletici | TCA9554 `0x20` — LCD RST/CS, TP RST, buzzer |
| OBD | UART TX=43, RX=44 @ 38400 |

Ayrıntı: [`UPLOAD.md` — Donanım](UPLOAD.md#donanım).

## Kurulum ve yükleme

### Gereksinimler

- **ESP-IDF 5.1+** (doğrulama: **5.3.5**)
- Python 3.8+, Ninja, USB (CP210x / CH340 / USB-JTAG)

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
| Sola / sağa kaydır | Sonraki / önceki **desteklenen** gösterge |
| Uzun basış (4 sn) | Varsayılan açılış göstergesini NVS’e kaydet |
| Alt noktalar + `n/N` | Aktif gösterge (N = bu araçta desteklenen sayı) |
| Çift dokunma | Menü |
| Menü / GERİ | Alt sayfalar |

Sabitler: `board_config.h` (`UI_SWIPE_THRESHOLD_PX`, `UI_LONG_PRESS_MS`, `UI_ROUND_INSET`).

## WiFi / ELM327

Referans: [Car Scanner — ELM327 Wi‑Fi](https://www.carscanner.info/wifi/)

### Kullanım akışı

| Adım | ESP32 monitor |
|------|----------------|
| Adaptör OBD soketine takılı, kontak açık | — |
| **Telefonu adaptör AP’sinden ayırın** (çoğu adaptör tek TCP istemcisi) | Otomatik bağlan = kapalı |
| Menü → **Bağlantı** → **Tara** | ELM327 adayları listelenir |
| Listeden **WIFI_OBDII** (veya adaptör SSID) seçin | WiFi → TCP → NVS’e kayıt |
| Başarılı | `Kayıtlı: WIFI_OBDII  192.168.0.10:35000` |
| Sonraki açılışlar | Yalnız kayıtlı profil denenir (rastgele tarama yok) |
| İsteğe bağlı | **Otomatik** düğmesi = tam ELM327 taraması |

**Varsayılan:** `wifi_manual_mode=true` — kayıtlı ağ yokken açılışta WiFi’ye bağlanmaz.

### TCP keşfi sırası

1. NVS’te kayıtlı IP:port  
2. DHCP **gateway** → `35000` (tam timeout + tekrar)  
3. Aynı alt ağ taraması (`.1`, `.10`, …)  
4. `elm327_tcp_profiles[]` yedek listesi  

Çoğu klon adaptör: **192.168.0.10:35000**, çoğu **OPEN** WiFi (şifre yok).

### Bağlantı günlüğü (`conn_log`)

Hata veya kısmi başarı NVS’e yazılır; **reboot sonrası** da okunur.

```powershell
.\build_flash.ps1 -Action monitor -Port COM3
```

Açılışta örnek:

```
W conn_log: ==== Connection log (2 entries) ====
W conn_log: #0 [t+15s] WiFi katildi: WIFI_OBDII
W conn_log: #1 [t+28s] TCP yok: ... (gw=192.168.0.10) telefonu ayirin
W conn_log: ==== end of connection log ====
```

Kod: `components/system/conn_log.c`, `wifi_manager.c` (`conn_log_add`).

### Notlar

- Kontak kapalı: “TCP hazır, kontağı açın” (RPM zorunlu değil).
- Timeout: `WIFI_CONNECT_TIMEOUT_MS` = **15 s** (`app.h`).

## Proje yapısı

```
esp32-obd2-monitor/
├── main/main.cpp
├── components/
│   ├── app/                  # Sabitler (poll, WiFi timeout, IP/port)
│   ├── connectivity/         # FSM, wifi/usb, elm327_session
│   ├── display/
│   │   ├── fonts/            # LVGL font .c (Türkçe + ikonlar; bkz. fonts/README.md)
│   │   ├── lvgl_port/
│   │   └── ui/
│   │       ├── dashboard.c, gauge.c, splash.c
│   │       ├── wifi_settings_ui.c, ui_icons.c, haptic.c
│   │       └── styles.c
│   ├── obd/
│   │   ├── obd_service.c     # Poll, keşif, valid bayrakları
│   │   ├── pid_table.c     # PID ↔ gösterge eşlemesi
│   │   ├── pid_support.c   # 0100/0120/0140 bitmap
│   │   └── obd_parser.c
│   ├── telemetry/
│   └── system/               # NVS settings, conn_log (bağlantı tanılama)
├── test/                     # Unity: test_obd_parser, test_pid_support
├── docs/
├── build_flash.ps1 / .sh
├── UPLOAD.md
└── README.md
```

## Çalışma modu

| Görev | Öncelik | Stack | Not |
|-------|---------|-------|-----|
| `display_init` | 5 | 16 KB | LVGL + splash + dashboard |
| `conn_reconnect` | 5 | 12 KB | Otomatik yeniden bağlanma |
| `obd_fast` | 6 | 4 KB | 40 ms — desteklenen hızlı PID |
| `obd_slow` | 4 | 4 KB | 2 s — desteklenen yavaş PID |
| `obd_dtc` | 3 | 4 KB | 30 s DTC |
| `gauge_update` | 4 | 8 KB | 25 Hz UI |
| `lvgl_handler` | 5 | 8 KB | core 1 |

Sabitler: `app.h` — `OBD2_FAST_POLL_MS`, `GAUGE_UPDATE_RATE_HZ`, `OBD2_DEFAULT_ADAPTER_*`.

## Yapılandırma

`idf.py menuconfig` → **OBD2 Monitor Settings** (isteğe bağlı SSID/IP override).

NVS: bağlantı tipi, WiFi profili, varsayılan gösterge, tema, parlaklık, haptic.

## Sorun giderme

Ayrıntı: **`UPLOAD.md` → Sorun giderme**.

| Belirti | İlk bakılacak |
|---------|----------------|
| Siyah ekran | TCA9554, PSRAM, ST7701 — UPLOAD.md |
| Boş / eksik metin | Font charset (`fonts/gen_fonts.ps1`, `0x20-0x7E,0xA0-0x17F`) |
| WiFi listede var, bağlanmıyor | Telefonu AP’den çıkar; `conn_log` dump (monitor); OPEN; 15 s timeout |
| WiFi var, TCP yok | Telefon adaptörde mi? Kontak açık mı? `conn_log` → gateway satırı |
| Tüm göstergeler `--` | Kontak; PID keşfi log; ELM327 protokol |
| Kaydırma 10 boş sayfa | Güncel firmware (evrensel PID filtresi) |

## Geliştirme

| Görev | Dosyalar |
|-------|----------|
| Yeni PID + gösterge | `pid_table.c`, `apply_pid` (`obd_service.c`), `gauge_configs[]` |
| Destek bitmap | `pid_support.c` |
| ELM327 / WiFi | `elm327_session.c`, `wifi_manager.c`, `elm327_wifi_profiles.h`, `conn_log.c` |
| UI / ikon | `ui_icons.c`, `dashboard.c`, `board_config.h` |
| Font üretimi | `components/display/fonts/README.md`, `gen_fonts.ps1` |
| Testler | `test/test_obd_parser.c`, `test/test_pid_support.c` |

## Lisans

MIT License
