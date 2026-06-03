# ESP32-S3 OBD2 Araç İzleme Sistemi v1.0.0

## Genel Bakış

Waveshare **ESP32-S3-Touch-LCD-2.1** kartı ile araç OBD2 verilerini gerçek zamanlı izleyen kapsamlı bir araç izleme sistemi. RGB 480x480 yuvarlak LCD ekran üzerinde racing-style gauge'ler ile tüm temel motor verilerini gösterir.

### Özellikler

- **Gerçek Zamanlı Veri**: RPM, Hız, Antifriz Sıcaklığı, Throttle Pozisyonu, Yakıt Seviyesi, Motor Yükü, Intake Sıcaklığı, MAF, Akü Voltajı
- **Hesaplanan Metrikler**: Anlık yakıt tüketimi (L/h)
- **DTC Okuma**: Motor arıza kodu tespiti ve uyarı göstergesi
- **3 Bağlantı Yöntemi**:
  - **WiFi**: OBD2 WiFi adaptörü (ELM327 otomatik tarama)
  - **Bluetooth SPP**: Klasik Bluetooth OBD2 adaptörü
  - **USB-UART**: USB üzerinden doğrudan OBD2 kablosu
- **480x480 RGB Yuvarlak LCD**: LVGL v9 tabanlı modern racing UI
- **Karanlık Tema**: Gece sürüşüne uygun kırmızı/turuncu/sarı renkler
- **5 Ekran**: Ana dashboard, Menü, Ayarlar, Bağlantı, Hakkında
- **NVS Kalıcı Ayarlar**: Tüm konfigürasyon flaşta saklanır
- **Otomatik Yeniden Bağlanma**: Bağlantı koparsa USB→WiFi→BT sırasıyla dener

### Desteklenen Araçlar

Özellikle **Chevrolet Kalos 2005** (ISO 9141-2 / KWP2000 protokolü) için test edilmiştir. Tüm standart OBD2 uyumlu araçlarla çalışır.

## Donanım Gereksinimleri

| Bileşen | Açıklama |
|---------|----------|
| **MCU** | ESP32-S3-Touch-LCD-2.1 (Waveshare) |
| **Ekran** | 480x480 RGB yuvarlak LCD, ST7701 |
| **Dokunmatik** | CST820 (I2C: SDA=15, SCL=7, INT=16) |
| **OBD2 Adaptör** | ELM327 WiFi/BT/USB (herhangi biri) |
| **Güç** | USB-C 5V veya pil |

### Pin Mapping (Waveshare ESP32-S3-Touch-LCD-2.1)

```
RGB LCD:  PCLK=41, DE=40, VSYNC=39, HSYNC=38
          B0-B5: NC,5,45,48,47,21
          G0-G5: 14,13,12,11,10,9
          R0-R5: NC,46,3,8,18,17
          Backlight: GPIO6
          Reset: EXIO1 (I/O expander)

Touch:    I2C SDA=15, SCL=7, INT=16, RST=EXIO2

OBD2 UART: TX=GPIO43, RX=GPIO44, Baud=38400
```

## Kurulum

### Gereksinimler

- **ESP-IDF 5.1+** (v5.2 veya v5.3 önerilir)
- **Python 3.8+**
- **CMake 3.16+**
- **Ninja Build**
- USB-C kablosu (programlama için)

### ESP-IDF Kurulumu (Windows PowerShell)

```powershell
# ESP-IDF v5.2 offline installer kullanın veya:
git clone -b v5.2 --recursive https://github.com/espressif/esp-idf.git C:\esp\esp-idf
C:\esp\esp-idf\install.ps1
. C:\esp\esp-idf\export.ps1
```

### Derleme ve Yükleme

#### Yöntem 1: Otomatik Script (Önerilen)

**Windows (PowerShell):**
```powershell
.\build_flash.ps1 -Action build
.\build_flash.ps1 -Action flash -Port COM3
.\build_flash.ps1 -Action monitor -Port COM3
```

**veya hepsini sırayla:**
```powershell
.\build_flash.ps1 -Action all -Port COM3
```

**Linux/macOS:**
```bash
chmod +x build_flash.sh
./build_flash.sh build
./build_flash.sh flash
./build_flash.sh monitor
```

#### Yöntem 2: Manuel

```bash
# ESP-IDF ortam değişkenlerini yükle
. $IDF_PATH/export.sh        # Linux/Mac
# veya PowerShell'de:
. C:\esp\esp-idf\export.ps1  # Windows

# Hedef MCU ayarla
idf.py set-target esp32s3

# Derle
idf.py build

# Yükle (port adını aygıtınıza göre değiştirin)
idf.py -p COM3 flash         # Windows
idf.py -p /dev/ttyUSB0 flash # Linux

# Seri monitör
idf.py -p COM3 monitor
```

### Port Tespiti

**Windows:** Aygıt Yöneticisi → "Ports (COM & LPT)" → USB Serial Device (COMx)

**Linux:** `ls /dev/ttyUSB*` veya `ls /dev/ttyACM*`

**macOS:** `ls /dev/cu.usb*`

## Proje Yapısı

```
esp32-obd2-monitor/
├── main/                          # Ana uygulama
│   ├── main.cpp                   # FreeRTOS task'ları (poll, gauge, diagnostic)
│   ├── CMakeLists.txt
│   └── Kconfig.projbuild          # Kullanıcı konfigürasyonu
├── components/
│   ├── app/                       # Paylaşılan tanımlar
│   │   ├── app.h                  # app_settings_t, bağlantı tipleri
│   │   └── app.c
│   ├── display/                   # LVGL ekran ve UI
│   │   ├── display.c              # Display başlatma
│   │   ├── lvgl_port/
│   │   │   ├── lvgl_driver.c      # ST7701 RGB panel sürücüsü
│   │   │   └── lvgl_driver.h
│   │   └── ui/
│   │       ├── styles.c           # Racing renk paleti ve stiller
│   │       ├── styles.h
│   │       ├── gauge.c            # 10 gauge widget'ı
│   │       ├── gauge.h
│   │       ├── dashboard.c        # 5 ekran oluşturma
│   │       └── dashboard.h
│   ├── obd/                       # OBD2 protokol
│   │   ├── obd_service.c          # PID okuma ve parsing
│   │   ├── obd_service.h
│   │   ├── pid_table.c            # PID metadata tablosu
│   │   └── pid_table.h
│   ├── connectivity/              # Bağlantı yönetimi
│   │   ├── connectivity.c         # Soyutlama katmanı
│   │   ├── connectivity.h
│   │   ├── wifi_manager.c         # ELM327 WiFi + AT komutları
│   │   ├── wifi_manager.h
│   │   ├── bt_manager.c           # Bluetooth SPP
│   │   ├── bt_manager.h
│   │   ├── usb_manager.c          # UART CDC
│   │   └── usb_manager.h
│   └── system/                    # Yardımcı servisler
│       ├── settings.c             # NVS ayar yönetimi
│       └── settings.h
├── assets/
│   └── fonts/                     # LVGL fontları (Montserrat)
├── sdkconfig.defaults             # ESP-IDF varsayılanları
├── build_flash.sh                 # Linux/macOS build script
├── build_flash.ps1                # Windows build script
└── README.md
```

## Yapılandırma

### Kconfig (idf.py menuconfig)

```
OBD2 Monitor Settings  --->
    WiFi SSID (OBD2_WIFI_SSID)         []
    WiFi Password (OBD2_WIFI_PASSWORD) []
    OBD2 Adapter IP (OBD2_ADAPTER_IP)  [192.168.0.10]
    OBD2 Adapter Port (OBD2_ADAPTER_PORT)  [35000]
    OBD2 UART Baudrate (OBD2_UART_BAUD)    [38400]
```

### OBD2 PID Tablosu

| PID | Açıklama | Birim | Formül |
|-----|----------|-------|--------|
| 0x0C | Motor RPM | rpm | (A*256+B)/4 |
| 0x0D | Araç Hızı | km/h | A |
| 0x05 | Antifriz Sıcaklığı | °C | A-40 |
| 0x0F | Intake Sıcaklığı | °C | A-40 |
| 0x11 | Throttle Pozisyonu | % | A*100/255 |
| 0x2F | Yakıt Seviyesi | % | A*100/255 |
| 0x04 | Motor Yükü | % | A*100/255 |
| 0x10 | MAF Rate | g/s | (A*256+B)/100 |
| 0x03 | DTC Codes | - | (Diagnostic) |

## Çalışma Modu

Sistem 3 paralel FreeRTOS göreviyle çalışır:

1. **obd_diagnostic_task** (öncelik 6, 8KB stack) — Başlangıçta ELM327 test, protokol tespiti, DTC okuma
2. **obd_polling_task** (öncelik 5, 4KB stack) — Her 200ms'de tüm PID'leri okur
3. **gauge_update_task** (öncelik 4, 4KB stack) — Her 100ms'de UI gauge'lerini günceller (10Hz)

## Sorun Giderme

### Build Hatası: "Component not found"

```bash
# Tüm componentler kayıtlı mı kontrol edin
ls components/*/CMakeLists.txt
# Çıktı 5 dosya göstermeli: app, connectivity, display, obd, system
```

### LCD Görüntü Gelmiyor

1. PSRAM'in çalıştığını doğrulayın: `idf.py monitor` çıktısında "PSRAM initialized" mesajı
2. Backlight GPIO6'nın HIGH olduğunu doğrulayın
3. RGB pin kablo bağlantılarını kontrol edin
4. `sdkconfig.defaults`'te `CONFIG_SPIRAM=y` ve `CONFIG_ESP32S3_DEFAULT_CPU_FREQ_240=y` olduğunu doğrulayın

### OBD2 Bağlantı Kurulamıyor

1. **WiFi**: ELM327 WiFi adaptörünün yayın yaptığını doğrulayın (SSID: "OBDII" veya "WiFi-ELM327")
2. **Bluetooth**: Klasik Bluetooth SPP uyumlu adaptör gerekli (BLE adaptörler çalışmaz)
3. **USB**: UART pinlerinin doğru bağlandığını kontrol edin (TX↔RX çapraz)
4. **Protokol**: `ATSP0` (auto-search) varsayılan olarak deneniyor, ELM327 firmware sürümünü doğrulayın

### Veri Gelmiyor ama Bağlantı Var

- Aracın kontağı açık olmalı (en azından "ON" pozisyonu)
- Bazı araçlar RPM'i sadece motor çalışırken verir
- ISO 9141-2 protokolü yavaş olabilir (5-10 PID/saniye beklenir)

## Geliştirme

### Yeni PID Eklemek

1. `components/obd/pid_table.h`'de `pid_type_t` enum'a PID ekle
2. `pid_table[]` dizisine metadata ekle
3. `obd_service.c`'de:
   - `cmd_xxx` static array tanımla
   - `parse_xxx()` fonksiyonu yaz
   - `obd_service_poll_all()` içinde çağır
4. `obd_data_t` struct'ına alan ekle
5. UI'da gauge eklemek için `gauge.c` düzenle

### Özel Gauge Tipi

`components/display/ui/gauge.c`'de `gauge_create_fullscreen()` fonksiyonuna yeni case ekle:

```c
case GAUGE_MY_NEW: {
    // LVGL objeleri oluştur (arc, label, vb.)
    break;
}
```

## Lisans

MIT License
