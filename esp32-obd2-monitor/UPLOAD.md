# ESP32-S3 OBD2 Monitor — Yükleme ve Derleme Notları

Bu dosya, projeyi **Waveshare ESP32-S3-Touch-LCD-2.1** kartına yüklerken yapılan adımları, **düzenleme geçmişini** ve sonraki oturumlar / AI asistanları için kritik bağlamı içerir.

**Resmi dokümantasyon:** [Waveshare ESP32-S3-Touch-LCD-2.1](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.1)

**Son başarılı yükleme:** COM3, hedef `esp32s3`, ESP-IDF v5.3.5 (Haziran 2026)

---

## Donanım

| Öğe | Değer |
|-----|--------|
| Kart | [Waveshare ESP32-S3-Touch-LCD-2.1](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.1) |
| MCU | ESP32-S3R8 — 16 MB Flash, 8 MB OPI PSRAM |
| Ekran | 480×480 RGB565 (ST7701), 3-wire SPI init + RGB paralel |
| Dokunmatik | CST820 (I2C `0x15`, INT GPIO16, RST EXIO2) |
| IO genişletici | **TCA9554PWR** (I2C `0x20`) — LCD RST/CS, TP RST |
| Arka ışık | GPIO6 (LEDC PWM, varsayılan %70) |
| I2C | SCL=GPIO7, SDA=GPIO15 (dokunmatik + expander + IMU + RTC paylaşımlı) |
| UART (OBD/USB) | TX=GPIO43, RX=GPIO44 |
| Flash partition | 2 MB uygulama |
| USB port | Windows: **COM3** (Aygıt Yöneticisi’nden kontrol edin) |

### TCA9554 EXIO eşlemesi (Waveshare wiki)

| EXIO | İşlev |
|------|--------|
| EXIO1 | LCD_RST |
| EXIO2 | TP_RST |
| EXIO3 | LCD_CS |
| EXIO4 | SD_CS |
| EXIO8 | Buzzer (active HIGH — kapalı = LOW) |

> **Buzzer:** Waveshare demo `Buzzer_Off()` → EXIO8 LOW. TCA9554 reset'te `0xFF` yapılırsa buzzer sürekli öter; başlangıç değeri `0x7F` + açılışta `BOARD_EXIO_BUZZER` LOW olmalı.

### Dolu I2C adresleri (harici cihaz eklerken çakışmayın)

`0x15` (CST820), `0x20` (TCA9554), `0x51`, `0x6B`, `0x7E`

---

## Geliştirme ortamı (Windows)

### Kurulu olanlar (bu makinede)

- **ESP-IDF:** `C:\Espressif\frameworks\esp-idf-v5.3.5`
- **Python (IDF):** `C:\Espressif\python_env\idf5.3_py3.11_env\Scripts\python.exe`
- **Araçlar:** `C:\Espressif\tools` (`IDF_TOOLS_PATH` buraya işaret etmeli)

### Her yeni PowerShell oturumunda

```powershell
$env:IDF_TOOLS_PATH = "C:\Espressif"
$env:Path = "C:\Espressif\python_env\idf5.3_py3.11_env\Scripts;" + $env:Path
. C:\Espressif\frameworks\esp-idf-v5.3.5\export.ps1
cd C:\Users\erdem\ESP32\alternative\esp32-obd2-monitor
```

> `export.ps1` çalışmazsa veya `idf.py` bulunamazsa: Python yolunu `Path`’e eklediğinizden ve `IDF_TOOLS_PATH` ayarlı olduğundan emin olun.

---

## Hızlı yükleme komutları

```powershell
# Sadece derle
idf.py build

# Derle + yükle (portu değiştirin)
idf.py -p COM3 flash

# Seri monitör (çıkış: Ctrl+])
idf.py -p COM3 monitor

# Derle + yükle + monitör
idf.py -p COM3 flash monitor
```

Proje kökündeki script (`esp32-obd2-monitor/build_flash.ps1`):

```powershell
.\build_flash.ps1 -Action build          # sadece derle
.\build_flash.ps1 -Action flash -Port COM3
.\build_flash.ps1 -Action monitor -Port COM3
.\build_flash.ps1 -Action all -Port COM3 # derle + flash + monitor
.\build_flash.ps1 -Action reconfigure    # build + sdkconfig sil + set-target
```

IDF yüklü değilse script `C:\Espressif\frameworks\esp-idf-v5.3.5\export.ps1` yolunu dener (bkz. Geliştirme ortamı).

---

## Hedef ve bağımlılıklar

| Ayar | Değer |
|------|--------|
| MCU hedefi | `esp32s3` (`idf.py set-target esp32s3`) |
| LVGL | Component Registry — `lvgl/lvgl` ^9.x |
| LCD sürücü | `espressif/esp_lcd_st7701`, `esp_lcd_panel_io_additions` |
| IO expander | Yerel `tca9554_expander.c` (legacy I2C, adres 0x20) |
| Dokunmatik | Yerel `cst820_touch.c` + `espressif/esp_lcd_touch` |
| `esp_io_expander` | **1.0.1** (legacy I2C uyumu; v2 yeni I2C sürücüsü kullanır) |
| Manifest | `main/idf_component.yml` |

İlk kurulumda veya hedef değişince:

```powershell
Remove-Item -Recurse -Force build, sdkconfig -ErrorAction SilentlyContinue
idf.py set-target esp32s3
idf.py build
```

---

## Partition tablosu

Firmware ~**1,3 MB**; varsayılan 1 MB factory partition yetersizdi.

- Dosya: `partitions.csv` (factory **2 MB** @ `0x10000`)
- `sdkconfig.defaults`: `CONFIG_PARTITION_TABLE_CUSTOM=y`

Partition değişince tam yeniden yapılandırma gerekir (`sdkconfig` silinip `set-target` tekrarlanır).

---

## Düzenleme notları (oturum geçmişi)

> Sonraki konuşmalarda bu bölümü referans alın. Kart **2.1"** modelidir; 4.3" kartlardaki CH422G sürücüsü **bu kartta çalışmaz**.

### Sorun: Siyah ekran — kök nedenler ve çözümler

| # | Sorun | Çözüm |
|---|--------|--------|
| 1 | ST7701 init yoktu (`esp_lcd_new_rgb_panel` doğrudan) | `esp_lcd_new_panel_st7701` + 3-wire SPI + Waveshare init komut dizisi |
| 2 | LCD RST/CS doğrudan GPIO'da değil | TCA9554 EXIO1 (RST), EXIO3 (CS) |
| 3 | Yanlış expander (CH422G @ 0x24/0x38) | **TCA9554 @ 0x20** — wiki ve demo doğrulandı |
| 4 | `esp_io_expander_tca9554` v2 + legacy I2C çakışması | Yerel `tca9554_expander.c` + `esp_io_expander` **1.0.1** |
| 5 | LVGL render döngüsü yoktu | `lvgl_handler_task` → `lv_timer_handler()` |
| 6 | `lv_display_flush_ready()` eksikti | Partial flush + `esp_lcd_panel_draw_bitmap()` |
| 7 | PSRAM QUAD modda | `CONFIG_SPIRAM_MODE_OCT=y` (OPI PSRAM) |
| 8 | `main` task stack overflow (~3.5 KB) | `display_init` → ayrı task, **16 KB** stack; `CONFIG_ESP_MAIN_TASK_STACK_SIZE=12288` |
| 9 | Buzzer sürekli ötüyordu | EXIO8 başlangıçta LOW (`0x7F` reset + `BOARD_EXIO_BUZZER`) |

### Başarılı boot log (referans)

```
I (940) tca9554: TCA9554 IO expander ready at 0x20
I (2000) lvgl_driver: LVGL initialized (480x480, CST820 touch)
I (2239) display: Display initialized
I (2330) main: Application started successfully
```

Hata örnekleri (artık düzeltilmiş):

```
E ch422g: mode write failed          → CH422G yanlış chip
E tca9554: write_output_reg failed   → v2 expander + legacy I2C
*** stack overflow in task main    → display_init main'de çağrılıyordu
```

### Display mimarisi (güncel)

```
app_main
  └─ display_init_task (16 KB stack)
       └─ display_init()
            ├─ lvgl_init()
            │    ├─ I2C init (GPIO7/15)
            │    ├─ tca9554_new_i2c_expander → buzzer OFF
            │    ├─ LCD reset + CS enable (EXIO)
            │    ├─ esp_lcd_new_panel_io_3wire_spi + esp_lcd_new_panel_st7701
            │    ├─ RGB panel init, CS disable
            │    ├─ LEDC backlight GPIO6 (%70)
            │    ├─ cst820_touch_init
            │    └─ LVGL display + touch indev (+ lv_indev_set_display)
            ├─ dashboard_init() [lvgl_lock]
            ├─ lv_refr_now()
            └─ lvgl_start() → lvgl_handler_task (core 1)

  └─ gauge_update_task (8 KB) → display_update_gauges() @ 10 Hz
```

### UI / dokunmatik (480×480 tam ekran)

| Özellik | Davranış |
|---------|----------|
| Kaydırma (≥50 px) | Gösterge önceki / sonraki (`dashboard_navigate_gauge_*`) |
| Alt nokta satırı | Aktif gösterge + `n/10` (kaydırma ile senkron) |
| Çift dokunma (~320 ms) | Menü ekranı |
| Menü kartları | Gösterge / Bağlantı / Ayarlar / Hakkında |
| GERİ | Ana gösterge ekranı |

Tema: **Workshop at Dusk** (`ui-demo.html` ile uyumlu amber/krem palet — `styles.c`).

Layout sabitleri: `board_config.h` → `UI_SCREEN_W/H`, `UI_SWIPE_THRESHOLD_PX`, `UI_DOUBLE_TAP_MS`.

| Dosya | Rol |
|-------|-----|
| `components/display/ui/dashboard.c` | 5 ekran, touch/swipe/menu olayları |
| `components/display/ui/gauge.c` | Tam ekran yay + indicator row + geçersiz `--` |
| `components/connectivity/connectivity.c` | Bağlantı FSM + `connectivity_get_state()` |
| `components/connectivity/elm327_session.c` | Paylaşılan AT init / OBD probe |
| `components/telemetry/telemetry.c` | UI snapshot (obd + bağlantı) |
| `components/display/ui/styles.c` | Renk paleti ve LVGL stilleri |

### Kritik kaynak dosyalar

| Dosya | Rol |
|-------|-----|
| `components/display/lvgl_port/board_config.h` | Pin / EXIO / I2C / UI layout sabitleri |
| `components/display/lvgl_port/lvgl_driver.c` | ST7701, LVGL, backlight, touch indev |
| `components/display/lvgl_port/tca9554_expander.c` | TCA9554 I2C sürücüsü (esp_io_expander API) |
| `components/display/lvgl_port/cst820_touch.c` | CST820 — Waveshare demo protokolü |
| `components/display/display.c` | `dashboard_init` + lock + `lvgl_start` |
| `components/display/ui/dashboard.c` | UI ekranları ve dokunma mantığı |
| `main/main.cpp` | `display_init_task`, OBD + gauge görevleri |
| `main/idf_component.yml` | LVGL 9, ST7701, esp_lcd_touch, esp_io_expander 1.0.1 |
| `sdkconfig.defaults` | OCT PSRAM, fontlar, stack, partition |
| `build_flash.ps1` / `build_flash.sh` | Derleme ve yükleme scriptleri |

### Waveshare demo referansı

Resmi demo indirme: [ESP32-S3-Touch-LCD-2.1-Code.zip](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.1/ESP32-S3-Touch-LCD-2.1-Code.zip)

Yararlı dosyalar (ESP-IDF klasörü):

- `main/EXIO/TCA9554PWR.c` — expander + buzzer
- `main/LCD_Driver/ST7701S.c` — init komutları, RGB timing
- `main/Touch_Driver/CST820.c` — dokunmatik protokolü
- `main/LVGL_Driver/LVGL_Driver.c` — double FB / flush örneği
- `sdkconfig.defaults.esp32s3` — PSRAM ayarları

### Henüz entegre edilmeyen kart özellikleri

OBD monitor kapsamı dışında; ileride eklenebilir:

| Donanım | Adres / Pin |
|---------|-------------|
| QMI8658 IMU | I2C `0x6B` |
| PCF85063 RTC | I2C `0x51` |
| TF kart | EXIO4 (SD_CS), SPI paylaşımlı GPIO1/2 |
| Batarya ADC | GPIO4 |
| Buzzer kontrol API | EXIO8 — şu an sadece boot'ta kapatılıyor |

### Bilinen tuzaklar (tekrar etme)

1. **CH422G kullanma** — sadece bazı Waveshare 4.x kartlarda; 2.1 = TCA9554.
2. **`esp_io_expander_tca9554` v2** — yeni I2C master ister; legacy `i2c_driver_install` ile çakışır.
3. **`sdkconfig` eski kalabilir** — `sdkconfig.defaults` değişince `idf.py reconfigure` veya `sdkconfig` sil.
4. **PSRAM QUAD vs OCT** — bu kart OCT; QUAD ile boot olur ama LCD/PSRAM buffer bozulabilir.
5. **Buzzer `0xFF`** — TCA9554 tüm pinleri HIGH yaparsa buzzer açık kalır.
6. **ESP32-S3 Classic BT / SPP yok** — OBD için WiFi veya UART kullan.
7. **I2C paylaşımlı** — touch, expander, IMU, RTC aynı bus; adres çakışmasına dikkat.

---

## Projede yapılan önemli düzeltmeler (özet)

Bu maddeler, “neden ilk hali derlenmiyordu?” sorusunun cevabıdır.

### 1. Eksik bileşenler

`components/lvgl`, `esp_lcd_st7701`, `esp_lcd_touch`, `esp_io_expander` klasörleri **yer tutucuydu** (gerçek kaynak yoktu). Kaldırıldı; gerçek paketler `idf_component.yml` ile indiriliyor.

### 2. CMake / IDF

- `esp_bt` → `bt` (doğru bileşen adı)
- `display` → `REQUIRES espressif__esp_lcd_st7701`
- Kök `CMakeLists.txt`: `-Wno-error=format` (BT log format uyarıları)

### 3. LVGL v9 uyumu

- Renkler: `LV_COLOR_MAKE` yerine `APP_COLOR_RGB` / `lv_color_hex` (`styles.h`)
- `lv_label_create(parent)` + `lv_label_set_text`
- `lv_switch_on` → `lv_obj_add_state(..., LV_STATE_CHECKED)`
- Fontlar: `sdkconfig.defaults` içinde Montserrat 10/12/16/20/24/48

### 4. C++ (`main.cpp`)

C header’lara `extern "C"` eklendi (`app.h`, `settings.h`, `display.h`, `obd_service.h`, `connectivity.h`, `dashboard.h`, `gauge.h`).

`main.cpp` görevleri: `obd_polling_task`, `obd_diagnostic_task`, `gauge_update_task` → `display_update_gauges()`.

### 5. Bluetooth (ESP32-S3 sınırı)

**ESP32-S3 klasik Bluetooth (SPP) desteklemez** — sadece BLE.  
`bt_manager.c` içinde `#if CONFIG_BT_CLASSIC_ENABLED` yoksa **stub** (WiFi/USB kullanın).

### 7. Ekran sürücüsü (Waveshare 2.1)

- **TCA9554** @ I2C `0x20` (CH422G değil — 2.1" kart farklı)
- ST7701: 3-wire SPI (GPIO1/2) + RGB (18 MHz PCLK)
- LVGL: partial flush + PSRAM draw buffer
- Arka ışık: LEDC PWM GPIO6
- `display_init` ayrı task'ta (16 KB stack) — stack overflow önlenir
- Pin tanımları: `components/display/lvgl_port/board_config.h`

### 8. Dokunmatik (CST820)

- I2C `0x15`, interrupt GPIO16, reset TCA9554 EXIO2
- Waveshare demo protokolü (`cst820_touch.c`)

### 9. sdkconfig.defaults (PSRAM)

```
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y
CONFIG_SPIRAM_RODATA=y
CONFIG_ESP_MAIN_TASK_STACK_SIZE=12288
CONFIG_FREERTOS_HZ=1000
```

### 10. Buzzer (EXIO8)

- Aktif HIGH; kapalı = LOW (`Buzzer_Off()` Waveshare demo ile aynı)
- `tca9554_expander.c` reset: `dev->output = 0x7F`
- `lvgl_driver.c` → `init_board_expander()`: `BOARD_EXIO_BUZZER` LOW

### 11. UI tam ekran ve dokunmatik (Haziran 2026)

| # | Sorun | Çözüm |
|---|--------|--------|
| 1 | Swipe / menü çalışmıyordu | `dashboard.c`: `LV_EVENT_PRESSED`/`RELEASED`, swipe eşiği 50 px, çift dokunma 320 ms |
| 2 | UI kayık / taşma | Gösterge `460×460 @ y=110` → toplam 570 px; `gauge.c` tam `480×480`, overlay status |
| 3 | Touch–display kopuk (LVGL 9) | `lv_indev_set_display(touch_handle, display_handle)` |
| 4 | Eski kırmızı tema | `styles.c` → Workshop at Dusk (`ui-demo.html` token’ları) |

---

| Yöntem | ESP32-S3 |
|--------|----------|
| WiFi (ELM327) | Desteklenir |
| USB-UART (GPIO43/44) | Desteklenir |
| Bluetooth SPP | **Desteklenmez** (donanım); menüde seçilirse bağlanmaz |

Varsayılan bağlantı tipi NVS’te (`settings`); ilk açılışta `connectivity_start(preferred_connection)` çağrılır.

---

## Sorun giderme

### `idf.py` bulunamıyor

Python env + `export.ps1` + `IDF_TOOLS_PATH=C:\Espressif`.

### `Failed to resolve component 'lvgl'`

`main/idf_component.yml` var mı? `idf.py set-target esp32s3` sonrası `managed_components/` oluşmalı.

### `app partition is too small`

`partitions.csv` ve `CONFIG_PARTITION_TABLE_CUSTOM` kontrol edin; `sdkconfig` yenileyin.

### Port bulunamıyor

```powershell
[System.IO.Ports.SerialPort]::GetPortNames()
```

Sürücü (CP210x / CH340 / USB-JTAG) ve kablo.

### Ekran boş / reboot döngüsü

- Seri monitörde `tca9554: TCA9554 IO expander ready at 0x20` görünmeli
- `stack overflow in task main` → `display_init` ayrı task'ta olmalı (güncel firmware)
- PSRAM: `CONFIG_SPIRAM_MODE_OCT=y` (QUAD değil OCT)
- Backlight: LEDC GPIO6 — monitörde `lvgl_driver: LVGL initialized`
- **CH422G kullanmayın** — bu kart TCA9554 kullanır ([Waveshare docs](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.1))

### Dokunmatik çalışmıyor

- Log: `cst820: chip id` / `CST820 touch ready`
- I2C bus paylaşımlı; başka sürücü I2C'yi kilitlememeli
- `lv_indev_set_display()` touch ile display eşleşmeli (`lvgl_driver.c`)
- Koordinat ters/offset ise `cst820_touch.c` → `flags.mirror_x` / `mirror_y` / `swap_xy`

### Kaydırma veya menü açılmıyor

- Sadece `GESTURE_BUBBLE` yeterli değil; güncel firmware `dashboard.c` içinde `LV_EVENT_PRESSED` / `RELEASED` kullanır
- Seri log: `Dashboard ready — swipe gauges, double-tap menu`
- Gösterge alanı taşması (eski layout) swipe’ı bozabilir; güncel `gauge.c` tam 480×480 kullanır

### Buzzer sürekli ötüyor

- EXIO8 (TCA9554 pin 7) boot'ta LOW olmalı
- `tca9554_expander.c` → `0x7F`, `board_config.h` → `BOARD_EXIO_BUZZER`

### Klasik BT linker hatası

ESP32-S3’te normal; `bt_manager.c` stub dalında derlenmeli. `CONFIG_BT_CLASSIC_ENABLED` S3’te etkin değildir.

---

## Sonraki geliştirme için dosyalar

| Dosya | Açıklama |
|-------|----------|
| `UPLOAD.md` | Bu dosya — yükleme + düzenleme notları |
| `components/display/lvgl_port/board_config.h` | Kart pin haritası |
| `main/idf_component.yml` | LVGL, ST7701, esp_lcd_touch bağımlılıkları |
| `partitions.csv` | 2 MB uygulama alanı |
| `sdkconfig.defaults` | S3, OCT PSRAM, font, stack, partition |
| `build_flash.ps1` | Windows build/flash script |
| `README.md` | Proje genel dokümantasyonu |

---

## Bağlantı durum makinesi (FSM)

```
DISCONNECTED → LINK_UP → ELM_INIT → OBD_READY
                  ↘ ERROR
```

- **LINK_UP:** WiFi AP + TCP veya USB UART açık, ELM327 `ATI` probe OK
- **OBD_READY:** `elm327_session` init + `010C` RPM probe OK
- UI pill rengi: kırmızı / amber / yeşil (`dashboard.c` HUD)
- Kopunca `obd_service_on_disconnect()` tüm `*_valid` bayraklarını temizler

Sabitler `app.h` ile senkron: `OBD2_FAST_POLL_MS` (40), `GAUGE_UPDATE_RATE_HZ` (25), `WIFI_CONNECT_TIMEOUT_MS` (7000).

### Parser birim testi

```powershell
idf.py test
```

`test/test_obd_parser.c` — örnek ELM327 cevabı `41 0C ...` ayrıştırma.

---

## Git / commit notu

Bu oturumda yapılan değişiklikler henüz commit edilmemiş olabilir. Commit öncesi:

```powershell
git status
git diff esp32-obd2-monitor
```

---

## Kısa kontrol listesi (tekrar yükleme)

1. USB bağlı, port doğru (COM3)
2. IDF ortamı yüklendi (`export.ps1` veya `build_flash.ps1` otomatik dener)
3. `cd esp32-obd2-monitor`
4. `idf.py -p COM3 flash` veya `.\build_flash.ps1 -Action all -Port COM3`
5. Kart reset olur; ekranda amber gösterge + üst WiFi/BT/USB pill’leri görünmeli, buzzer susmalı
6. **Kaydır** → gösterge değişir; **çift dokun** → menü; **GERİ** → ana ekran
7. Seri log: `Application started successfully` ve `Dashboard ready — swipe gauges, double-tap menu`

---

*Son güncelleme: Haziran 2026 — display bring-up (TCA9554/ST7701/CST820), tam ekran UI, swipe/çift dokunma, `build_flash` scriptleri.*
