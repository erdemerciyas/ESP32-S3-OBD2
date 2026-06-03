<div align="center">

# ESP32-S3 OBD2 Monitor

**Real-time vehicle telemetry on a 480×480 round RGB LCD — ELM327 over WiFi or USB-UART, with touch UI and Waveshare board bring-up.**

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.2%20%7C%20v5.3-E7352C?logo=espressif&logoColor=white)](https://github.com/espressif/esp-idf)
[![MCU](https://img.shields.io/badge/ESP32--S3-000000?logo=espressif&logoColor=white)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Display](https://img.shields.io/badge/LVGL-v9-FF6F00?logo=lvgl&logoColor=white)](https://lvgl.io/)
[![License](https://img.shields.io/badge/license-MIT-22C55E)](LICENSE)
[![Repo](https://img.shields.io/badge/GitHub-erdemerciyas-181717?logo=github)](https://github.com/erdemerciyas/ESP32-S3-OBD2)

A racing-style digital instrument cluster for any OBD-II compliant vehicle, built on the
[Waveshare ESP32-S3-Touch-LCD-2.1](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.1) dev kit.
Three independent OBD-II transport options, ten live PIDs, full DTC diagnostics, and a dark-themed
gauge suite designed to be readable in a night-driving cockpit.

</div>

---

## Highlights

- **Ten live PIDs** — RPM, vehicle speed, coolant & intake temperature, throttle position, fuel
  level, engine load, MAF rate, intake MAP and fuel pressure polled at 5 Hz.
- **DTC diagnostics** — ELM327 handshake, automatic protocol detection and reading of stored
  diagnostic trouble codes with on-screen warnings.
- **Two OBD-II transports (ESP32-S3)** — WiFi (ELM327 AP scan, built-in SSID/IP profiles,
  on-screen WiFi settings) and USB-UART (GPIO43/44) behind a unified `connectivity` layer.
  Classic Bluetooth SPP is not available on ESP32-S3; use a WiFi or wired ELM327 adapter.
- **Round 480×480 RGB LCD** — ST7701 + TCA9554 IO expander, CST820 touch, boot splash,
  custom LVGL v9 port with OCT PSRAM framebuffer, 10 Hz gauge refresh.
- **Persistent NVS settings** — Wi-Fi credentials, preferred transport, theme and brightness
  survive power cycles.
- **Five screens** — Main dashboard, menu, settings, connection and about — all driven from
  a custom racing-style gauge widget.
- **Tested on Chevrolet Kalos 2005** (ISO 9141-2 / KWP2000). Works with any standard OBD-II
  compliant vehicle.

---

## Repository layout

```
ESP32-S3-OBD2/
├── README.md                     # you are here — landing page
├── .gitignore                    # ESP-IDF build artefacts
└── esp32-obd2-monitor/           # full ESP-IDF project
    ├── main/                     # app entry, FreeRTOS tasks
    ├── components/
    │   ├── app/                  # shared types (app_settings_t, transports)
    │   ├── display/              # LVGL port, styles, gauge & dashboard UI
    │   ├── obd/                  # PID table, OBD-II service, ELM327 parser
    │   ├── connectivity/         # WiFi (ELM327 profiles) / USB-UART managers
    │   └── system/               # NVS-backed settings store
    ├── assets/fonts/             # Montserrat LVGL fonts
    ├── partitions.csv            # 2 MB factory app partition
    ├── main/idf_component.yml    # LVGL 9, ST7701, esp_lcd_touch (Component Manager)
    ├── sdkconfig.defaults        # OCT PSRAM, fonts, ST7701, stack sizes
    ├── build_flash.sh            # Linux / macOS build helper
    ├── build_flash.ps1           # Windows PowerShell build helper
    ├── ui-demo.html              # static UI mockup (open in a browser)
    ├── UPLOAD.md                 # flash / bring-up / troubleshooting (Turkish)
    └── README.md                 # project documentation (Turkish)
```

The full Turkish reference — pin map, Kconfig options, FreeRTOS task layout, PID table and
troubleshooting matrix — lives in [`esp32-obd2-monitor/README.md`](esp32-obd2-monitor/README.md).
Flash/build session notes and Waveshare bring-up history: [`esp32-obd2-monitor/UPLOAD.md`](esp32-obd2-monitor/UPLOAD.md).

---

## Hardware

| Component        | Detail                                                      |
|------------------|-------------------------------------------------------------|
| **Dev kit**      | Waveshare ESP32-S3-Touch-LCD-2.1                            |
| **Display**      | 480×480 round RGB LCD, ST7701 controller                    |
| **Touch**        | CST820 (I²C `0x15`: SDA=15, SCL=7, INT=16, RST=EXIO2)       |
| **IO expander**  | TCA9554 (`0x20`) — LCD RST/CS, touch RST, buzzer (EXIO)     |
| **OBD-II link**  | ELM327 — WiFi AP (auto profiles) or USB-UART                |
| **Power**        | USB-C 5 V or battery pack                                   |

### Pin map (Waveshare ESP32-S3-Touch-LCD-2.1)

```
RGB LCD :  PCLK=41, DE=40, VSYNC=39, HSYNC=38
           B0–B5 : NC, 5, 45, 48, 47, 21
           G0–G5 : 14, 13, 12, 11, 10, 9
           R0–R5 : NC, 46, 3, 8, 18, 17
           Backlight : GPIO6
           Reset     : EXIO1 (I/O expander)

Touch   :  I²C SDA=15, SCL=7, INT=16, RST=EXIO2

OBD-II  :  UART TX=GPIO43, RX=GPIO44, baud=38400
```

---

## Build & flash

### Prerequisites

- **ESP-IDF v5.2 or v5.3** ([install guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/index.html))
- Python 3.8+, CMake 3.16+, Ninja
- USB-C cable for programming

### Quick start

```powershell
# Windows — use the helper script
cd esp32-obd2-monitor
.\build_flash.ps1 -Action all -Port COM3
```

```bash
# Linux / macOS
cd esp32-obd2-monitor
chmod +x build_flash.sh
./build_flash.sh build
./build_flash.sh flash
./build_flash.sh monitor
```

### Manual

```bash
. $IDF_PATH/export.sh                  # Linux/macOS
# . C:\esp\esp-idf\export.ps1          # Windows PowerShell

cd esp32-obd2-monitor
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash                   # adjust port for your machine
idf.py -p COM3 monitor
```

Want to preview the UI before flashing? Open `esp32-obd2-monitor/ui-demo.html` in a browser —
it's a static, fully styled mock-up of the gauge cluster.

---

## Architecture in one diagram

```
                        ┌──────────────────────────────┐
                        │       app_main (main)        │
                        └──────────────┬───────────────┘
                                       │
              ┌────────────────────────┼────────────────────────┐
              ▼                        ▼                        ▼
   obd_diagnostic_task        obd_polling_task        gauge_update_task
   (prio 6, 8 KB)             (prio 5, 4 KB · 5 Hz)   (prio 4, 4 KB · 10 Hz)
              │                        │                        │
              └──────── connectivity ──┴──── display/LVGL ──────┘
                           ▲   ▲   ▲
                           │   │   │
                  ┌────────┘   │   └────────┐
                  ▼            ▼            ▼
            wifi_manager              usb_manager
            (ELM327 AP + profiles)    (UART 43/44)
```

A unified `connectivity` layer hides the active transport behind
`connectivity_send_cmd()` / `connectivity_is_connected()`, so the OBD service is transport-agnostic.

---

## Supported OBD-II PIDs

| PID   | Name              | Unit   | Formula            |
|-------|-------------------|--------|--------------------|
| 0x04  | Engine load       | %      | A · 100 / 255      |
| 0x05  | Coolant temp      | °C     | A − 40             |
| 0x0A  | Fuel pressure     | kPa    | A · 3              |
| 0x0B  | Intake MAP        | kPa    | A                  |
| 0x0C  | Engine RPM        | rpm    | (A·256 + B) / 4    |
| 0x0D  | Vehicle speed     | km/h   | A                  |
| 0x0E  | Timing advance    | °      | A / 2 − 64         |
| 0x0F  | Intake air temp   | °C     | A − 40             |
| 0x10  | MAF rate          | g/s    | (A·256 + B) / 100  |
| 0x11  | Throttle position | %      | A · 100 / 255      |
| 0x1F  | Engine runtime    | s      | A·256 + B          |
| 0x2F  | Fuel level        | %      | A · 100 / 255      |
| 0x03  | DTC codes         | —      | diagnostic service |

---

## Configuration

All user-facing knobs live under `OBD2 Monitor Settings` in `idf.py menuconfig`:

```
OBD2_WIFI_SSID          []           # SSID of your ELM327 WiFi adapter
OBD2_WIFI_PASSWORD      []           # password (leave blank for open APs)
OBD2_ADAPTER_IP         [192.168.0.10]
OBD2_ADAPTER_PORT       [35000]
OBD2_UART_BAUD          [38400]
```

Runtime preferences (preferred transport, theme, brightness, haptic) are stored in NVS and
edited on the device from the **Settings** screen.

---

## Troubleshooting

- **No image on the LCD** — confirm `CONFIG_SPIRAM=y` and
  `CONFIG_ESP32S3_DEFAULT_CPU_FREQ_240=y` in `sdkconfig.defaults`, and that backlight GPIO 6
  is HIGH.
- **OBD-II never connects** — try a different ELM327 firmware revision, force `ATSP0` for
  protocol auto-search, and verify the ignition is at least in the **ON** position.
- **Connected but no data** — some vehicles only publish RPM while the engine is running;
  ISO 9141-2 is slow, expect 5–10 PIDs/s.
- **Component not found at build** — `ls components/*/CMakeLists.txt` should report
  `app`, `connectivity`, `display`, `obd`, `system`.

A full troubleshooting matrix is in the project README.

---

## Contributing

1. Fork the repo and create a feature branch.
2. Follow the existing component layout — new PIDs go in `components/obd`, new gauges in
   `components/display/ui/gauge.c`.
3. Keep public APIs behind the existing headers; the `connectivity` and `obd_service`
   abstractions are intentional.
4. Test on real hardware before opening a PR (a Kalos 2005 is available for verification).

## License

Released under the [MIT License](LICENSE).
