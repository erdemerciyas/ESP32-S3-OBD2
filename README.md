# ESP32-S3 OBD-II Dashboard

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.3.5-blue)](https://idf.espressif.com/)
[![LVGL](https://img.shields.io/badge/LVGL-v8.4-green)](https://lvgl.io/)
[![Board](https://img.shields.io/badge/Board-Waveshare_ESP32--S3--Touch--LCD--2.1-orange)](https://www.waveshare.com/esp32-s3-touch-lcd-2.1.htm)

A real-time automotive OBD-II (On-Board Diagnostics) dashboard built on the **ESP32-S3** with a **480×480 round LCD**, running **ESP-IDF v5.3.5** and **LVGL v8.4**. It connects to a vehicle's ECU via a **BLE ELM327 adapter**, reads dozens of PIDs, and displays them on a touch-interactive gauge user interface.

---

## Features

### Dashboard (Main Gauge)
- **Center gauge** — 420px arc gauge with 270° sweep displaying either **RPM** or **Speed**
- **Double-tap** to toggle between RPM and Speed in the center gauge
- **Gradient arc coloring**: arc color changes smoothly based on value:
  - **RPM**: Green → Yellow → Orange → Red (3000/4000/6000/7000 thresholds)
  - **Speed**: Green → Yellow → Orange → Red (80/100/120/160 kmh thresholds)
- **94px bold** center value display with custom-generated Montserrat Bold font
- **3 bottom stat cells** showing live values that swap contextually with center gauge

### Live Data Grid
- 3×3 grid displaying **9 real-time OBD-II PIDs**:
  - Throttle Position (TPS %)
  - Manifold Absolute Pressure (MAP kPa)
  - Engine Load (%)
  - Intake Air Temperature (IAT)
  - Ignition Timing (°)
  - Short Term Fuel Trim (STFT %)
  - Long Term Fuel Trim (LTFT %)
  - Oxygen Sensor 1 Voltage (O2 V)
  - Oxygen Sensor 2 Voltage (O2 V)

### Fault Codes (DTC)
- Read active and pending Diagnostic Trouble Codes from the ECU
- Clear stored fault codes
- Auto-scan when navigating to the DTC tab (with 8s debounce)
- Supports up to **16 DTCs** with pending code tracking

### Connection Screen
- BLE device scanning and connection management
- Animated status arc showing connection progress
- Status display for OBD states: Scanning, Connecting, ELM Init, PID Discovery, Ready, Error

### Settings
- **Metric/Imperial units** toggle (km/h ↔ mph, °C ↔ °F)
- **Auto-connect** toggle for BLE adapter
- **Center gauge source** toggle (RPM ↔ Speed)
- System info display: profile, protocol, adapter, voltage, DTC count

### Temperature Critical Alert
- **Buzzer beeps** immediately when coolant temperature reaches critical threshold
- Beep duration: **400ms**
- If still critical after **30 seconds**, beeps again
- Stops immediately when temperature returns to normal
- Uses the board's **TCA9554 IO expander** (EXIO8 = pin 7) to drive the buzzer

### Bluetooth LE (NimBLE)
- BLE GATT client using **Apache NimBLE** stack
- Connects to BLE ELM327 adapters (e.g., VEEPEAK, OBDLink, etc.)
- Central role with observer mode for scanning
- Maximum 1 simultaneous connection
- PSRAM-based memory allocation for BLE stack

---

## Hardware Requirements

| Component | Specification |
|-----------|---------------|
| **MCU** | ESP32-S3 (Xtensa LX7 dual-core @ 240 MHz) |
| **Display** | 480×480 round LCD (ST7701) via RGB interface |
| **Touch** | Capacitive touch panel |
| **PSRAM** | Octal PSRAM @ 80 MHz (required for LVGL buffers) |
| **Flash** | 16 MB |
| **BLE** | Built-in BLE (NimBLE stack) |
| **Buzzer** | Connected to IO expander TCA9554 (EXIO8 / pin 7) |
| **Board** | [Waveshare ESP32-S3-Touch-LCD-2.1](https://www.waveshare.com/esp32-s3-touch-lcd-2.1.htm) |

---

## Pin Mapping (Waveshare ESP32-S3-Touch-LCD-2.1)

| Signal | GPIO | Notes |
|--------|------|-------|
| LCD RGB | RGB565 | Parallel RGB interface via `esp_display_panel` |
| Touch I2C | GPIO 19 (SDA), GPIO 20 (SCL) | CST226SE or similar |
| IO Expander I2C | Same I2C bus | TCA9554, address 0x27 |
| Buzzer (EXIO8) | TCA9554 pin 7 | Active high via IO expander |

---

## Software Architecture

```
app_main()
├── nvs_flash_init()
├── vehicle_data_init()          ← Thread-safe shared data store
├── bsp_display_init()           ← Board, LCD, touch, LVGL init
│   └── bsp_buzzer_init()        ← IO expander + buzzer setup
├── ui_init()                    ← Create all screens
│   ├── screen_connect_create()  ← BLE connection UI
│   ├── screen_dash_create()     ← Main gauge + stats
│   ├── screen_grid_create()     ← Live data 3×3 grid
│   ├── screen_dtc_create()      ← Fault code viewer
│   └── screen_settings_create() ← Configuration
├── ui_start_update_timer()      ← 50ms periodic update
├── ble_obd_init/start()         ← BLE GATT client
├── elm327_init/start()          ← ELM327 command processor
├── obd_pids_init/start()        ← PID polling scheduler
└── obd_dtc_init()               ← DTC read/clear
```

### Module Breakdown

- **`main/`** — Entry point, component registration
- **`main/bsp/`** — Board support: display init, LVGL port, buzzer (C++ with `esp_display_panel`)
- **`main/data/`** — Vehicle data model, vehicle profile system, application logging
- **`main/obd/`** — BLE ELM327 communication, PID polling engine, DTC read/clear
- **`main/ui/`** — LVGL screens, theme system, custom fonts

### Screens (Tab Navigation)

Navigation uses an LVGL TabView with hidden tab buttons. Screen switching is done via **swipe gesture** (horizontal) and visual indicator dots at the bottom:

1. **Connection** — BLE scan, connect, status
2. **Dashboard** — Main arc gauge + stats row
3. **Live Data** — 3×3 PID grid
4. **Fault Codes** — DTC list with scan/clear
5. **Settings** — Toggles and system info

---

## Custom Fonts

Two custom Montserrat fonts were generated using `lv_font_conv` for the gauge display:

| Font | Size | Weight | Usage |
|------|------|--------|-------|
| `lv_font_montserrat_56` | 56px | Regular | Larger labels |
| `lv_font_montserrat_94_bold` | 94px | Bold | Center gauge value |

These are compiled directly into the firmware (no external file system required).

---

## Color System

### Theme Palette

| Token | Color | HEX |
|-------|-------|-----|
| Background | Dark blue-black | `#04080C` |
| Surface | Dark navy | `#0C141E` |
| Surface Highlight | Lighter navy | `#141E2C` |
| Primary (teal) | Teal | `#00D4AA` |
| Secondary (blue) | Light blue | `#38BDF8` |
| Text | Off-white | `#F1F5F9` |
| Text Dim | Gray-blue | `#8B9AB0` |
| OK / Good | Green | `#22C55E` |
| Warning | Yellow | `#FACC15` |
| Critical | Red | `#EF4444` |
| Arc Background | Deep navy | `#162030` |
| Border | Border blue | `#203044` |

### Speed Gradient

| Speed (km/h) | Arc Color |
|--------------|-----------|
| 0 – 80 | Green → Green |
| 80 – 100 | Green → Yellow (lerp) |
| 100 – 120 | Yellow → Orange (lerp) |
| 120 – 160 | Orange → Red (lerp) |
| 160+ | Red |

### RPM Gradient

| RPM | Arc Color |
|-----|-----------|
| 0 – 3000 | Green |
| 3000 – 4000 | Green → Yellow (lerp) |
| 4000 – 6000 | Yellow → Orange (lerp) |
| 6000 – 7000 | Orange → Red (lerp) |
| 7000+ | Red |

---

## OBD-II PID Support

The system supports dynamic PID discovery and polling. The following PIDs are polled:

| PID | Description | Priority |
|-----|-------------|----------|
| 0x0C | RPM (engine speed) | High (live) |
| 0x0D | Vehicle speed | High (live) |
| 0x05 | Coolant temperature | High (live) |
| 0x42 | Battery voltage | High (live) |
| 0x11 | Throttle position | Medium |
| 0x0B | MAP (intake pressure) | Medium |
| 0x0F | Intake air temperature | Medium |
| 0x0E | Timing advance | Medium |
| 0x04 | Engine load | Medium |
| 0x06 | Short term fuel trim | Medium |
| 0x07 | Long term fuel trim | Medium |
| 0x14 | O2 sensor 1 voltage | Medium |
| 0x15 | O2 sensor 2 voltage | Medium |
| 0x03 | Fuel system status | Slow |
| 0x12 | Secondary air status | Slow |
| 0x0A | Fuel pressure | Slow |

---

## Getting Started

### Prerequisites

- **ESP-IDF v5.3.5** installed (with ESP32-S3 support)
- Python 3.11+ with required packages
- Git

### Build & Flash

```bash
# Clone the repository
git clone https://github.com/erdemerciyas/ESP32-S3-OBD2.git
cd ESP32-S3-OBD2

# Set up ESP-IDF environment (Windows PowerShell example)
. C:\Espressif\frameworks\esp-idf-v5.3.5\export.ps1

# Configure for your board (if not using default Waveshare board)
idf.py menuconfig

# Build
idf.py build

# Flash (connect ESP32-S3 via USB)
idf.py -p COM3 flash

# Monitor serial output
idf.py -p COM3 monitor
```

> **Note**: This project is configured for the **Waveshare ESP32-S3-Touch-LCD-2.1** board. If using a different board, update the panel configuration in `menuconfig` under `Component config > ESP Panel`.

### Partition Table

| Partition | Type | Offset | Size |
|-----------|------|--------|------|
| NVS | data | 0x9000 | 24 KB |
| PHY init | data | 0xF000 | 4 KB |
| Factory app | app | 0x10000 | 3 MB |

---

## Project Dependencies (Managed Components)

| Component | Version | Purpose |
|-----------|---------|---------|
| `espressif/esp32_display_panel` | Latest | LCD, touch, IO expander driver |
| `espressif/esp32_io_expander` | Latest | TCA9554 IO expander C++ API |
| `espressif/esp-lib-utils` | Latest | Utility helpers |
| `lvgl/lvgl` | v8.4 | Graphics library |

---

## Configuration

Key configuration options in `sdkconfig.defaults`:

| Option | Value | Description |
|--------|-------|-------------|
| `CONFIG_IDF_TARGET` | `esp32s3` | Target chip |
| `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ` | `240` | CPU frequency |
| `CONFIG_SPIRAM` | `y` | PSRAM enabled |
| `CONFIG_SPIRAM_MODE_OCT` | `y` | Octal PSRAM mode |
| `CONFIG_SPIRAM_XIP_FROM_PSRAM` | `y` | Execute-in-place from PSRAM |
| `CONFIG_BT_NIMBLE_ENABLED` | `y` | NimBLE BLE stack |
| `CONFIG_LVGL_PORT_AVOID_TEARING_MODE_3` | `y` | Double-buffer + direct mode |

---

## Troubleshooting

- **Display not initializing**: Check that `CONFIG_ESP_PANEL_BOARD_MANUFACTURER_WAVESHARE=y` and `CONFIG_BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_2_1=y` are set.
- **BLE scanning finds no devices**: Ensure the ELM327 adapter is in pairing mode. Check `CONFIG_BT_NIMBLE_ROLE_CENTRAL=y` and `CONFIG_BT_NIMBLE_ROLE_OBSERVER=y`.
- **LVGL out of memory error**: Verify PSRAM is enabled and `CONFIG_LV_MEM_CUSTOM=y` is set so LVGL uses PSRAM.
- **Text not displaying on gauge**: Custom fonts (`montserrat_56`, `montserrat_94_bold`) must be compiled in. Check `main/CMakeLists.txt` includes them in `SRCS`.

---

## License

This project is open source. See the repository for license details.

---

## Author

**erdemerciyas** — [GitHub](https://github.com/erdemerciyas)
