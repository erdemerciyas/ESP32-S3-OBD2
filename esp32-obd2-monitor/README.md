# ESP32-S3 OBD2 Araç İzleme Sistemi v1.0.0

Waveshare **[ESP32-S3-Touch-LCD-2.1](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.1)** kartı ile araç OBD2 verilerini gerçek zamanlı izleyen gösterge paneli. 480×480 RGB ekranda LVGL 9 arayüzü.

> **Yükleme, donanım pinleri, sorun giderme ve oturum notları:** [`UPLOAD.md`](UPLOAD.md) — bu dosyayı flash/build işlemleri için birincil kaynak olarak kullanın.

## Genel Bakış

### Özellikler

- **Gerçek zamanlı PID’ler:** RPM, hız, antifriz, gaz, yakıt, yük, emme sıcaklığı, akü, yakıt tüketimi, DTC
- **Tam ekran gösterge:** 10 parametre; kaydırma veya alt çizgilere dokunarak geçiş
- **Dokunmatik menü:** Çift dokunma → menü; 4 alt ekran (Bağlantı, Ayarlar, Hakkında)
- **Bağlantı:** WiFi (ELM327 tarama, `elm327_wifi_profiles.h`, ekrandan SSID seçimi), USB-UART (GPIO43/44)
- **Bluetooth SPP:** ESP32-S3 klasik BT desteklemez — OBD için WiFi veya USB ELM327 kullanın
- **Açılış animasyonu:** `splash.c` boot splash, ardından ana gösterge
- **Tema:** Workshop at Dusk (amber/krem, `ui-demo.html` ile uyumlu)
- **NVS ayarları**, WiFi tercihleri ve otomatik yeniden bağlanma

### Desteklenen araçlar

Özellikle **Chevrolet Kalos 2005** (ISO 9141-2 / KWP) için hedeflenmiştir; standart OBD2 uyumlu araçlarla çalışır.

## Donanım

| Bileşen | Açıklama |
|---------|----------|
| Kart | Waveshare ESP32-S3-Touch-LCD-2.1 |
| Ekran | 480×480 ST7701 RGB + 3-wire SPI init |
| Dokunmatik | CST820, I2C `0x15` |
| IO genişletici | TCA9554 `0x20` — LCD RST/CS, TP RST, buzzer |
| OBD | UART TX=43, RX=44 @ 38400 |

Ayrıntılı pin / EXIO tablosu: [`UPLOAD.md` — Donanım](UPLOAD.md#donanım).

## Kurulum ve yükleme

### Gereksinimler

- **ESP-IDF 5.1+** (bu projede **5.3.5** ile doğrulandı)
- Python 3.8+, Ninja, USB sürücüsü (CP210x / CH340 / USB-JTAG)

### Windows (önerilen)

Her yeni PowerShell oturumunda (veya `build_flash.ps1` ortamı kendisi yükler):

```powershell
$env:IDF_TOOLS_PATH = "C:\Espressif"
$env:Path = "C:\Espressif\python_env\idf5.3_py3.11_env\Scripts;" + $env:Path
. C:\Espressif\frameworks\esp-idf-v5.3.5\export.ps1
cd esp32-obd2-monitor   # depo kökünden
```

```powershell
# Script ile (UPLOAD.md ile aynı)
.\build_flash.ps1 -Action build -Port COM3
.\build_flash.ps1 -Action all -Port COM3

# veya doğrudan
idf.py build
idf.py -p COM3 flash monitor
```

İlk kurulum / hedef değişimi:

```powershell
.\build_flash.ps1 -Action reconfigure
```

### Linux / macOS

```bash
. $IDF_PATH/export.sh
cd esp32-obd2-monitor
chmod +x build_flash.sh
./build_flash.sh all /dev/ttyUSB0
```

## Dokunmatik kullanım

| Hareket | Sonuç |
|---------|--------|
| Sola kaydır | Sonraki gösterge |
| Sağa kaydır | Önceki gösterge |
| Alt çizgi | İlgili göstergeye atla |
| Çift dokunma | Menü |
| Menü kartı / GERİ | Alt sayfa / ana ekran |

## Proje yapısı

```
esp32-obd2-monitor/
├── main/
│   ├── main.cpp              # display_init_task, OBD, gauge_update
│   ├── idf_component.yml     # LVGL 9, ST7701, esp_lcd_touch, …
│   └── CMakeLists.txt
├── components/
│   ├── display/
│   │   ├── display.c
│   │   ├── lvgl_port/
│   │   │   ├── board_config.h
│   │   │   ├── lvgl_driver.c
│   │   │   ├── tca9554_expander.c
│   │   │   └── cst820_touch.c
│   │   └── ui/
│   │       ├── dashboard.c   # Ekranlar + touch
│   │       ├── gauge.c
│   │       ├── splash.c        # Boot splash
│   │       ├── wifi_settings_ui.c
│   │       └── styles.c
│   ├── obd/
│   ├── connectivity/           # elm327_wifi_profiles.h
│   ├── system/
│   └── app/
├── partitions.csv            # 2 MB factory app
├── sdkconfig.defaults        # OCT PSRAM, fontlar, stack
├── build_flash.ps1
├── build_flash.sh
├── UPLOAD.md                 # Yükleme + düzenleme geçmişi
├── ui-demo.html              # Tarayıcı UI önizlemesi
└── README.md
```

## Çalışma modu

| Görev | Öncelik | Stack | Görev |
|-------|---------|-------|--------|
| `display_init_task` | 5 | 16 KB | LVGL + dashboard (bir kez) |
| `obd_diagnostic_task` | 6 | 8 KB | DTC / protokol |
| `obd_polling_task` | 5 | 4 KB | PID okuma (200 ms) |
| `gauge_update_task` | 4 | 8 KB | UI güncelleme (10 Hz) |
| `lvgl_handler_task` | 5 | 8 KB | `lv_timer_handler` (core 1) |

## Yapılandırma

`idf.py menuconfig` → **OBD2 Monitor Settings** (WiFi SSID, adaptör IP/port, UART baud).

Varsayılan bağlantı tipi NVS’te; `connectivity_start(preferred_connection)` ile açılışta başlar.

## Sorun giderme

Özet tablolar ve log örnekleri **`UPLOAD.md` → Sorun giderme** bölümünde.

| Belirti | İlk bakılacak |
|---------|----------------|
| Siyah ekran | TCA9554 log, OCT PSRAM, ST7701 init — UPLOAD.md |
| `idf.py` yok | `export.ps1`, `IDF_TOOLS_PATH` |
| Partition küçük | `partitions.csv` + custom partition |
| Dokunmatik yok | CST820 chip id, I2C `0x15` |
| Kaydırma yok | Güncel `dashboard.c` event handler’ları |
| Buzzer ötüyor | EXIO8 boot’ta LOW (`0x7F`) |
| BT bağlanmıyor | ESP32-S3 SPP desteklemez — WiFi/USB |

## Geliştirme

- Yeni PID: `pid_table`, `obd_service.c`, `gauge_configs[]` (`gauge.c`)
- UI sabitleri: `board_config.h`
- Canlı HTML önizleme: `ui-demo.html` (tarayıcıda açın)

## Lisans

MIT License
