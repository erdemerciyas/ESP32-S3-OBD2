# ESP32-S3 OBD2 Dashboard (Waveshare 2.1" Touch LCD)

Arduino / PlatformIO project for **Waveshare ESP32-S3-Touch-LCD-2.1** (480×480, ST7701, LVGL 8.x). Connects to a **WiFi ELM327** OBD-II adapter over TCP and shows live vehicle data on a round LVGL dashboard.

## Features

- WiFi connect with automatic retry
- ELM327 TCP client (default `192.168.0.10:35000`, fallback ports **35000**, **23**, **35000**)
- ELM init: `ATZ`, `ATE0`, `ATL0`, `ATS0`, `ATH0`, `ATSP0`
- Continuous PID polling: RPM, speed, coolant, intake, throttle, battery voltage (`ATRV`), engine load (`0104`), MAP (`010B`)
- Vehicle screen (long-press): VIN (`0902`), DTC read (`0100` + `03`), MIL clear (`04`) — ported from [VaAndCob OBD gauge](https://github.com/VaAndCob/ESP32-Bluetooth-OBD2-Gauge)
- Modular architecture: `wifi_manager`, `elm327_client`, `obd_service`, `obd_extras`, `dashboard_ui`, `display_hal`
- Non-blocking `loop()` — LVGL runs on its own task via ESP32_Display_Panel port

## Project layout

```
esp-ard/
├── platformio.ini
├── boards/BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_2_1.json
├── include/
│   ├── config.h          # WiFi, ELM327 host, timings
│   ├── wifi_manager.h
│   ├── elm327_client.h
│   ├── obd_service.h
│   ├── obd_parse.h / obd_formulas.h / obd_extras.h
│   ├── vehicle_info_ui.h
│   ├── dashboard_ui.h
│   └── display_hal.h
└── src/
    ├── main.cpp
    ├── wifi_manager.cpp
    ├── elm327_client.cpp
    ├── obd_service.cpp
    ├── dashboard_ui.cpp
    ├── display_hal.cpp
    ├── lvgl_v8_port.cpp / .h / lv_conf.h
```

## Tak-çalıştır (telefon gerekmez)

1. **OBD WiFi adaptörünü** çalıştırın (araçta kontak açık).
2. ESP32’yi güç verin → otomatik **OBDII / V-LINK / WiFi_OBDII** vb. ağlara bağlanır.
3. TCP üzerinden ELM327’ye bağlanır; gösterge verileri gelir.

Adaptör SSID listede yoksa `include/config.h` içine yazın:

```c
#define OBD_WIFI_SSID     "SizinAdaptörSSID"
#define OBD_WIFI_PASSWORD "sifreniz"
```

Son başarılı OBD ağı ESP32 flash’ına kaydedilir; bir sonraki açılışta önce o denenir.

## Build & upload (manuel)

```powershell
pio run -t upload -e waveshare_lcd_21
pio device monitor
```

Port otomatik algılanmıyorsa `platformio.ini` içinde `upload_port` / `monitor_port` satırını düzenleyin (şu an **COM3**).

## Hardware

- [Waveshare ESP32-S3-Touch-LCD-2.1](https://www.waveshare.com/esp32-s3-touch-lcd-2.1.htm)
- WiFi OBD2 adapter in TCP server mode (often port 35000 or 23)

## UI

Round **480×480** panel ([Waveshare 2.1"](https://www.waveshare.com/esp32-s3-touch-lcd-2.1.htm)), **Phosphor / Workshop at Dusk** theme (parity with `esp32-obd2-monitor/ui-demo.html`):

- Center arc gauge (270°), amber phosphor readout
- **Swipe** left/right: RPM → Speed → Coolant → Battery → Throttle → Intake → Engine load → MAP
- Indicator dots + status pills (WiFi / OBD)
- **Double-tap**: WiFi menu
- **Long-press**: Vehicle info (VIN, DTC, clear MIL)

## Troubleshooting

- **No OBD data**: ignition ON, adapter paired to same WiFi, correct ELM327 IP
- **Display tear / ghosting**: use `LVGL_PORT_AVOID_TEARING_MODE=1` (double buffer + VSYNC full refresh) in `platformio.ini`; bounce buffer = `width * 10`. OBD logs are batched (5 s) to reduce flash/LCD bus contention
- **WiFi fails**: check SSID/password; ESP32 and adapter must share the network

## License

Application code: MIT. Third-party libs (LVGL, ESP32_Display_Panel) follow their respective licenses.
