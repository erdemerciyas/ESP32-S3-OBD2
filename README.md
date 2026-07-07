# ESP32-S3 OBD-II Dashboard

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.3.5-blue)](https://idf.espressif.com/)
[![LVGL](https://img.shields.io/badge/LVGL-v8.4-green)](https://lvgl.io/)
[![Board](https://img.shields.io/badge/Board-Waveshare_ESP32--S3--Touch--LCD--2.1-orange)](https://www.waveshare.com/esp32-s3-touch-lcd-2.1.htm)

A real-time automotive OBD-II (On-Board Diagnostics) dashboard built on the **ESP32-S3** with a **480×480 round LCD**, running **ESP-IDF v5.3.5** and **LVGL v8.4**. It connects to a vehicle's ECU via a **BLE ELM327 adapter**, reads dozens of PIDs, and displays them on a touch-interactive gauge user interface.

**Universal OBD-II profile** with automatic protocol detection (`ATSP0`) and runtime PID discovery works with any ELM327-compatible vehicle. Saved **vehicle profiles** (protocol, timeouts, redline, supported PID masks) let you switch between different cars or engines instantly.

---

## Features

### Dashboard (Main Gauge)
- **Center gauge** — large 270° sweep arc that fills the round LCD, displaying either **RPM** or **Speed**
- **Gauge is centered** on the 480×480 round panel; the status bar and data strip are overlaid on top of the arc
- **Double-tap** the gauge to toggle between RPM and Speed
- **Active profile name** centered at the top of the gauge; **Bluetooth status** icon on the top-right
- **9-segment shift-light strip** above the RPM value, lighting up as the engine approaches redline
- **Redline zone** rendered as a subtle arc band past the profile's `rpm_redline`
- **Gradient arc coloring**: arc color changes smoothly based on value:
  - **RPM**: Cyan → Orange → Red (3000/5000/6500 thresholds)
  - **Speed**: Cyan → Orange → Red (80/120/180 km/h thresholds)
- **94px bold** center value display with custom-generated Montserrat Bold font
- **3 bottom stat cards** with colored top-accent borders and separate value/unit labels:
  - Left card toggles opposite to the center gauge (shows **Speed** in RPM mode and **RPM** in Speed mode)
  - Center and right cards show **Coolant** and **Voltage**
  - Coolant and voltage cards tint automatically on warning/critical thresholds

### Live Data Grid
- 3×3 grid of **card-style cells** displaying **9 real-time OBD-II PIDs**
- Each cell has a colored **left-accent border** and separate value/unit labels
- Displayed PIDs:
  - Throttle Position (TPS %)
  - Manifold Absolute Pressure (MAP kPa)
  - Engine Load (%)
  - Intake Air Temperature (IAT °C/°F)
  - Ignition Timing (°)
  - Short Term Fuel Trim (STFT %)
  - Long Term Fuel Trim (LTFT %)
  - Oxygen Sensor 1 Voltage (O2 V)
  - Oxygen Sensor 2 Voltage (O2 V)

### Connection Screen
- BLE device scanning and connection management
- Animated status arc showing connection progress with color-coded states
- Status text color reflects state: ready (green), error (red), progress (blue)
- Full-width primary **Scan for adapter** button
- Status display for OBD states: Scanning, Connecting, ELM Init, PID Discovery, Ready, Error

### Settings
- **Vehicle profile** dropdown to switch between saved profiles instantly
- **Metric/Imperial units** toggle (km/h ↔ mph, °C ↔ °F)
- **Auto-connect** toggle for BLE adapter
- **Center gauge source** toggle (RPM ↔ Speed)
- Setting rows styled with colored left-accent borders and themed switches
- System info display: profile, protocol, adapter, voltage

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

### IMU Off-Road Screen (QMI8658)
- **6-axis IMU** (accelerometer + gyroscope) via I2C
- Displays **pitch** and **roll** angles with twin arc gauges
- **Yaw** (heading) display with gyro integration
- Real-time sensor diagnostics (accel XYZ, gyro XYZ, temperature)
- **Gyro calibration** on startup (vehicle must be stationary)
- NVS storage for pitch/roll offset calibration
- 100 Hz polling task for smooth updates

### Splash Screen
- Animated loading screen with spinning arc
- 3-second duration before transitioning to main UI
- Smooth fade-in animation

---

## Hardware Requirements

| Component | Specification |
|-----------|---------------|
| **MCU** | ESP32-S3 (Xtensa LX7 dual-core @ 240 MHz) |
| **Display** | 480×480 round LCD (ST7701) via RGB interface |
| **Touch** | Capacitive touch panel (CST226SE) |
| **IMU** | QMI8658 6-axis (accel ±8g + gyro ±512 dps) |
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
| IMU I2C | GPIO 19 (SDA), GPIO 20 (SCL) | QMI8658, address 0x6A (shared with touch) |
| IO Expander I2C | Same I2C bus | TCA9554, address 0x27 |
| Buzzer (EXIO8) | TCA9554 pin 7 | Active high via IO expander |

---

## Software Architecture

```
app_main()
├── nvs_flash_init()
├── vehicle_data_init()          ← Thread-safe shared data store
├── vehicle_profile_init()       ← Load saved profiles + universal profile
├── bsp_display_init()           ← Board, LCD, touch, LVGL init
│   └── bsp_buzzer_init()        ← IO expander + buzzer setup
├── imu_init()                   ← QMI8658 IMU driver + gyro calibration
├── ui_init()                    ← Create all screens
│   ├── screen_splash_create()   ← Animated loading screen
│   ├── screen_connect_create()  ← BLE connection UI
│   ├── screen_dash_create()     ← Main gauge + stats
│   ├── screen_grid_create()     ← Live data 3×3 grid
│   ├── screen_gyro_create()     ← IMU pitch/roll/yaw display
│   └── screen_settings_create() ← Configuration
├── ui_start_update_timer()      ← 16ms periodic update (60 fps)
├── imu_start()                  ← Start IMU polling task
├── ble_obd_init/start()         ← BLE GATT client
├── elm327_init/start()          ← ELM327 command processor
└── obd_pids_init/start()        ← PID polling scheduler
```

### Module Breakdown

- **`main/`** — Entry point, component registration
- **`main/bsp/`** — Board support: display init, LVGL port, buzzer (C++ with `esp_display_panel`)
- **`main/data/`** — Vehicle data model, vehicle profile system, application logging
- **`main/obd/`** — BLE ELM327 communication and PID polling engine
- **`main/imu/`** — QMI8658 6-axis IMU driver, gyro calibration, sensor fusion
- **`main/ui/`** — LVGL screens, theme system, custom fonts

### Project Structure

```
├── main/                  # ESP32-S3 firmware (OBD, UI, BSP, IMU)
├── simulator/             # LVGL PC simulator (Visual Studio, shared UI sources)
├── scripts/               # verify_round_lcd_layout.py — round LCD geometry check
├── docs/                  # GELISTIRME_KURALLARI.md — development rules
├── rebuild.bat            # Windows: fullclean + build + flash
├── CHANGELOG.md           # Dated project history and current status
├── partitions.csv         # Flash partition table
├── sdkconfig.defaults     # Default ESP-IDF configuration
└── dependencies.lock      # Managed component versions
```

Reference snapshots, build logs, and duplicate scripts were removed in the 2026-07 cleanup (~1500 files). See `CHANGELOG.md` for details.

### Screens (Tab Navigation)

Navigation uses an LVGL TabView with hidden tab buttons. Screen switching is done via **swipe gesture** (horizontal) and visual indicator dots at the bottom:

1. **Connection** — BLE scan, connect, status
2. **Dashboard** — Main arc gauge + stats row
3. **Live Data** — 3×3 PID grid
4. **Gyro** — IMU pitch/roll/yaw off-road display
5. **Settings** — Toggles and system info

A **splash screen** plays for 3 seconds at startup, then transitions to the main UI.

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
| Background | Near black | `#02060A` |
| Surface | Dark navy | `#0A1018` |
| Surface Highlight | Lighter navy | `#121C28` |
| Primary | Cyan | `#00F0FF` |
| Secondary | Orange | `#FF9100` |
| Accent | Racing red | `#FF2A2A` |
| Text | Off-white | `#F0F0F0` |
| Text Dim | Gray-blue | `#6B7A8F` |
| OK / Good | Green | `#00E676` |
| Warning | Amber | `#FFC400` |
| Critical | Racing red | `#FF2A2A` |
| Arc Background | Deep navy | `#08121C` |
| Border | Border blue | `#1C2A3C` |

### Speed Gradient

| Speed (km/h) | Arc Color |
|--------------|-----------|
| 0 – 80 | Cyan |
| 80 – 120 | Cyan → Orange (lerp) |
| 120 – 180 | Orange → Red (lerp) |
| 180+ | Red |

### RPM Gradient

| RPM | Arc Color |
|-----|-----------|
| 0 – 3000 | Cyan |
| 3000 – 5000 | Cyan → Orange (lerp) |
| 5000 – 6500 | Orange → Red (lerp) |
| 6500+ | Red |

---

## OBD-II PID Support

The system supports dynamic PID discovery and polling with **EMA + spike filtering** for all PIDs. The following PIDs are polled:

| PID | Description | Priority | Filter Alpha | Spike Max |
|-----|-------------|----------|--------------|-----------|
| 0x0C | RPM (engine speed) | High (live) | 0.90 | 1500 RPM |
| 0x0D | Vehicle speed | High (live) | 0.94 | 30 km/h |
| 0x05 | Coolant temperature | High (live) | 0.55 | 15°C |
| 0x42 | Battery voltage | High (live) | 0.30 | 0.6V |
| 0x11 | Throttle position | Medium | 0.70 | 30% |
| 0x0B | MAP (intake pressure) | Medium | 0.70 | 30 kPa |
| 0x0F | Intake air temperature | Medium | 0.55 | 20°C |
| 0x0E | Timing advance | Medium | 0.60 | 15° |
| 0x04 | Engine load | Medium | 0.65 | 30% |
| 0x06 | Short term fuel trim | Medium | 0.40 | 15% |
| 0x07 | Long term fuel trim | Medium | 0.40 | 15% |
| 0x14 | O2 sensor 1 voltage | Medium | 0.50 | 0.8V |
| 0x15 | O2 sensor 2 voltage | Medium | 0.50 | 0.8V |
| 0x10 | MAF (air flow) | Medium | 0.60 | 30 g/s |
| 0x03 | Fuel system status | Slow | 0.00 | — |
| 0x12 | Secondary air status | Slow | 0.00 | — |
| 0x0A | Fuel pressure | Slow | 0.40 | 80 kPa |

**Filtering**: Each PID uses an Exponential Moving Average (EMA) filter with spike rejection. Cold-start seeding provides instant initial values. After 3 consecutive spikes, the filter resets to track rapid real changes.

---

## Getting Started

### Prerequisites

- **ESP-IDF v5.3.5** installed (with ESP32-S3 support)
- Python 3.11+ with required packages
- Git

### Build & Flash

**Windows (recommended):** use `rebuild.bat` for a full clean, reconfigure, build, and flash in one step. Edit the COM port in the script if needed (default: `COM4`).

```powershell
.\rebuild.bat
```

**Manual (any platform):**

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

### PC Simulator

UI changes can be tested on Windows without flashing hardware. The simulator shares `main/ui/` sources with the firmware.

**Requirements:** Visual Studio 2022+ with **Desktop development with C++**, platform **x64**.

```powershell
cd simulator
.\build_simulator.cmd
.\Output\Binaries\Release\x64\LVGL.Simulator.exe
```

Or open `simulator/LVGL.Simulator.sln` in Visual Studio and press F5. A 480×480 window opens with simulated OBD data (RPM, speed, coolant, etc.).

See `simulator/README.md` for setup details and troubleshooting.

### Layout Verification

```powershell
python scripts/verify_round_lcd_layout.py
```

Checks dashboard UI regions against the 480×480 round LCD geometry defined in `main/ui/theme.h`.

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

## Development Notes

- **`CHANGELOG.md`** — dated history, current build/flash status, open tasks
- **`docs/GELISTIRME_KURALLARI.md`** — rules for preserving BLE/ELM327 connection stability
- **`.cursor/rules/`** — Cursor IDE guidance (changelog, BLE stability)

When modifying OBD connection code (`ble_obd.c`, `elm327.c`), keep changes minimal and verify on real hardware.

---

This project is open source. See the repository for license details.

---

## Author

**erdemerciyas** — [GitHub](https://github.com/erdemerciyas)
