# ESP32-S3 OBD2 Dashboard — ELM327 Bluetooth (BLE) & WiFi Car Gauge on a Round Touch LCD

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.3.5-blue)](https://idf.espressif.com/)
[![LVGL](https://img.shields.io/badge/LVGL-v8.4-green)](https://lvgl.io/)
[![Board](https://img.shields.io/badge/Board-Waveshare_ESP32--S3--Touch--LCD--2.1-orange)](https://www.waveshare.com/esp32-s3-touch-lcd-2.1.htm)
[![Adapter](https://img.shields.io/badge/Adapter-ELM327_BLE_%7C_WiFi-purple)](#supported-elm327-adapters)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Release](https://img.shields.io/github/v/release/erdemerciyas/ESP32-S3-OBD2)](https://github.com/erdemerciyas/ESP32-S3-OBD2/releases/latest)

**Website:** [erdemerciyas.github.io/ESP32-S3-OBD2](https://erdemerciyas.github.io/ESP32-S3-OBD2/) · **Prebuilt firmware:** [latest release](https://github.com/erdemerciyas/ESP32-S3-OBD2/releases/latest)

An open-source **ESP32-S3 OBD-II car dashboard** for the **Waveshare ESP32-S3-Touch-LCD-2.1** (480×480 round touch display). It reads live engine data from any OBD-II car through a cheap **ELM327 clone adapter — Bluetooth Low Energy (BLE) or WiFi** — and shows it as a full-screen digital gauge cluster: tachometer, speedometer, coolant / oil temperature, battery voltage, live sensor data, **trouble code (DTC) reader and eraser**, and an off-road inclinometer.

Built with **ESP-IDF v5.3.5**, **LVGL v8.4** and the **NimBLE** stack. No phone, no app, no cloud: plug the adapter into the OBD2 port, power the ESP32 from USB, and drive.

> 🇹🇷 Türkçe özet [aşağıda](#türkçe-özet).

---

## Table of contents

- [Highlights](#highlights)
- [Screens](#screens)
- [Supported ELM327 adapters](#supported-elm327-adapters)
- [Hardware](#hardware)
- [Getting started](#getting-started)
- [Configuration](#configuration)
- [How it works](#how-it-works)
- [OBD-II data & PIDs](#obd-ii-data--pids)
- [Project structure](#project-structure)
- [PC simulator](#pc-simulator)
- [Troubleshooting](#troubleshooting)
- [Türkçe özet](#türkçe-özet)

---

## Highlights

- **Two adapter types, one firmware** — BLE ELM327 (FFF0 / FFE0 GATT profiles) *and* WiFi ELM327 (TCP `192.168.0.10:35000`). Pick one in **Settings → Link**; the other radio is shut down and the new one starts searching immediately, without a reboot.
- **Universal OBD-II** — automatic protocol detection (`ATSP0`), detected protocol cached in flash (`ATSPA<n>`) so the next connection is instant. Works with CAN (ISO 15765), K-line KWP2000 (ISO 14230), ISO 9141-2, J1850.
- **Only what your car supports** — PIDs are discovered at connect time (`0100/0120/0140…`); unsupported values are hidden and never polled.
- **Smooth, synchronized gauges** — RPM and speed are interpolated over the measured sample interval and move in lock-step at ~60 FPS; EMA + spike filtering on every PID.
- **Diagnostic trouble codes** — reads stored (`03`), pending (`07`) and freeze-frame (`02`) data, MIL status and readiness monitors; clears codes (`04`) with verification; keeps a history in flash. ~190 code descriptions (generic SAE P0/P2 + GM-Daewoo P1xxx), currently in Turkish.
- **Slow-protocol tuning** — prompt gating, response-count suffix (`010C1`), slot budgeting so K-line cars still get ~6–8 requests/s with RPM/speed first.
- **Round-display UI** — every screen is designed for the circular 460 px visible area: ring segments, a central lens, dot navigation, swipe between screens.
- **Robust links** — BLE: saved-address direct connect, scan fallback, GATT watchdog, backoff. WiFi: SSID auto-detect, DHCP → static-IP fallback, TCP keepalive, `TCP_NODELAY`, modem-sleep disabled for low latency.
- **Extras** — coolant over-temperature buzzer, battery over/under-voltage warnings, QMI8658 pitch/roll inclinometer, animated ignition-style boot sequence.

---

## Screens

Swipe left/right to move between screens. The order is:

| # | Screen | What it shows |
|---|--------|---------------|
| 1 | **Connect** | Four-stage link ring (SCAN · LINK · ELM · PIDS), radar sweep while busy, BLE or WiFi icon for the selected link. Tap the centre to re-scan. |
| 2 | **Dash** | 270° tachometer arc with shift lights and redline zone, big primary value (RPM or speed — double-tap to swap), secondary value, coolant · oil · battery cards, protocol and request rate, `⚠ N ARIZA` fault badge (tap → DTC). |
| 3 | **Live Data** | Ring of supported sensors (throttle, load, MAP, intake temp, oil temp, timing, MAF, short/long fuel trim, O2 sensors, fuel level). Tap a chip to focus it in the centre lens with session min/max; long-press resets min/max. |
| 4 | **DTC** | "Supernova" scan view: colour tells the state (cyan scanning, green clean, yellow pending, orange stored, red critical/MIL). Code cards → detail (description, likely cause, freeze-frame), history, clear-codes dialog. |
| 5 | **Gyro** | Off-road inclinometer: pitch and roll rings, rotating horizon, zero button. |
| 6 | **Settings** | Tiles: **Units** (metric/imperial) · **Link** (BLE / WiFi) · **Auto** connect · **Centre** gauge (RPM/speed) · **Profile**. Below: adapter, state, protocol, raw voltage source, req/s, RPM Hz, timeouts. |

---

## Supported ELM327 adapters

### Bluetooth Low Energy (BLE)

- Any BLE ELM327 that exposes a serial-like GATT service: `FFF0` (notify `FFF1`, write `FFF2`) or `FFE0` (`FFE1`/`FFE2`). This covers most "ELM327 V1.5 / V2.1 BLE", Vgate iCar Pro BLE, Veepeak BLE and similar clones.
- Found by name (`OBD`, `OBDII`, `ELM`, `VLINK`, `IOS-Vlink`, …) or by service UUID. The address is saved; next time the dashboard connects directly.
- Classic Bluetooth (SPP-only) adapters are **not** supported — the ESP32-S3 has no Bluetooth Classic radio.

### WiFi

Typical WiFi ELM327 clones run their own open access point and bridge TCP to the ELM chip:

| Setting | Usual value |
|---------|-------------|
| SSID | `WiFi_OBDII`, `OBDII`, `OBD2`, `V-LINK` (Vgate) |
| Security | open (some use a password) |
| Adapter IP | `192.168.0.10` (some `192.168.0.11`) |
| TCP port | `35000` |

The firmware scans for an AP whose name contains `OBD`, `ELM`, `V-LINK`, `VLINK`, `ICAR`, `VGATE` or `KONNWEI`, joins the strongest one, and connects to the DHCP gateway (falling back to `192.168.0.10`). If the adapter's DHCP server doesn't answer within 5 s, a static address in the adapter's subnet is used. Everything can be pinned in `menuconfig` (see [Configuration](#configuration)).

> **Only one client at a time.** WiFi adapters accept a single TCP connection — disconnect any phone app (Torque, Car Scanner…) first.

---

## Hardware

| Component | Details |
|-----------|---------|
| Board | [Waveshare ESP32-S3-Touch-LCD-2.1](https://www.waveshare.com/esp32-s3-touch-lcd-2.1.htm) |
| MCU | ESP32-S3, dual-core Xtensa LX7 @ 240 MHz, WiFi 2.4 GHz + BLE 5 |
| Memory | 16 MB flash, 8 MB octal PSRAM @ 80 MHz |
| Display | 2.1" 480×480 round IPS, ST7701 RGB interface |
| Touch | CST816S capacitive (I2C) |
| IMU | QMI8658 6-axis (±8 g, ±512 dps) |
| Buzzer | via TCA9554 IO expander (EXIO8) |
| OBD adapter | ELM327 clone, BLE or WiFi |
| Power | USB-C (5 V) — in the car a USB socket or 12 V → 5 V converter |

| Signal | GPIO / bus | Notes |
|--------|------------|-------|
| LCD | RGB565 parallel | via `esp32_display_panel` |
| Touch, IMU, IO expander | I2C, GPIO 19 (SDA) / 20 (SCL) | QMI8658 `0x6A`, TCA9554 `0x27` |
| Buzzer | TCA9554 pin 7 | active high |

---

## Getting started

### Option A — flash the prebuilt firmware (no toolchain)

Download `esp32s3-obd2-dashboard-merged.bin` from the [latest release](https://github.com/erdemerciyas/ESP32-S3-OBD2/releases/latest) and write it at offset `0x0`:

```bash
pip install esptool
esptool.py --chip esp32s3 -p COM3 write_flash 0x0 esp32s3-obd2-dashboard-merged.bin
```

Use the board's USB port (Linux/macOS: `/dev/ttyACM0`, `/dev/cu.usbmodem…`). Browser-based tools such as the [ESP Web Flasher](https://espressif.github.io/esptool-js/) also work: same file, address `0x0`.

### Option B — build from source

#### Prerequisites

- [ESP-IDF **v5.3.5**](https://docs.espressif.com/projects/esp-idf/en/v5.3.5/esp32s3/get-started/index.html) with ESP32-S3 support
- Python 3.11 (installed by the ESP-IDF installer), Git

#### Build & flash

```bash
git clone https://github.com/erdemerciyas/ESP32-S3-OBD2.git
cd ESP32-S3-OBD2

# ESP-IDF environment (Windows PowerShell example; use export.sh on Linux/macOS)
. C:\Espressif\frameworks\esp-idf-v5.3.5\export.ps1

idf.py build
idf.py -p COM3 flash monitor
```

Managed components (`esp32_display_panel`, `lvgl`) are downloaded automatically on the first build.

**Windows shortcut:** `rebuild.bat` does fullclean → reconfigure → build → flash. Edit the paths and COM port inside if your install differs.

> **Windows note:** run `idf.py` from PowerShell or `cmd`; Git Bash/MSYS is rejected by ESP-IDF.
> On the Waveshare board the USB-JTAG port carries the application console; the CH343 UART port flashes but only shows the ROM banner.

### First run in the car

1. Plug the ELM327 adapter into the OBD2 port (usually under the steering column) and turn the ignition **on**.
2. Power the dashboard. After the boot animation it opens the **Connect** screen and starts searching.
3. Using a **WiFi** adapter? Swipe to **Settings** and tap **Link** until it shows **WiFi**. The choice is saved.
4. Once the ring is complete (SCAN → LINK → ELM → PIDS) the dashboard switches to the gauge.

---

## Configuration

Runtime settings live on the device (Settings screen) and are stored in NVS. Build-time options are under `idf.py menuconfig` → **OBD2 Dashboard Configurations → OBD adapter link**:

| Option | Default | Meaning |
|--------|---------|---------|
| `OBD_LINK_DEFAULT_WIFI` | `n` | Transport on first boot (until changed on the device) |
| `OBD_WIFI_SSID` | empty | Fixed adapter SSID; empty = scan for OBD-like names |
| `OBD_WIFI_PASSWORD` | empty | For adapters with a secured AP |
| `OBD_WIFI_HOST` | empty | Fixed adapter IP; empty = DHCP gateway, then `192.168.0.10` |
| `OBD_WIFI_PORT` | `35000` | Adapter TCP port |

Notable `sdkconfig.defaults` choices:

| Option | Why |
|--------|-----|
| `CONFIG_SPIRAM=y`, `CONFIG_SPIRAM_MODE_OCT=y`, `CONFIG_SPIRAM_XIP_FROM_PSRAM=y` | LVGL frame buffers and code in PSRAM |
| `CONFIG_BT_NIMBLE_ENABLED=y`, `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y` | lightweight BLE central, buffers in PSRAM |
| `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` | WiFi/LWIP buffers in PSRAM |
| `CONFIG_LVGL_PORT_AVOID_TEARING_MODE_3=y` | double buffer + direct mode, no tearing |
| `CONFIG_LV_DISP_DEF_REFR_PERIOD=16` | ~60 FPS refresh |

Vehicle profiles (protocol command, ELM timeout, redline, gauge limits) are in `main/data/vehicle_profile.c`; the default is **Universal OBD-II**.

---

## How it works

```
 ┌──────────────┐   BLE GATT (NimBLE)   ┌─────────────┐
 │ ELM327 BLE   │◄─────────────────────►│ ble_obd.c   │──┐
 └──────────────┘                       └─────────────┘  │   obd_link.c
 ┌──────────────┐   TCP :35000 (lwIP)   ┌─────────────┐  ├─► (one transport
 │ ELM327 WiFi  │◄─────────────────────►│ wifi_obd.c  │──┘    active at a time)
 └──────────────┘                       └─────────────┘           │
                                                                  ▼
                       elm327.c  — command queue, prompt gating, response parser
                                                                  │
                     obd_pids.c  — PID discovery, scheduling, decode, EMA filter
                     obd_dtc.c   — DTC scan / clear / history
                                                                  │
                  vehicle_data.c — thread-safe snapshot  ◄── imu_data.c (QMI8658)
                                                                  │
                         ui/*.c  — LVGL screens, 16 ms update timer (core 1)
```

- OBD tasks run on core 0, LVGL on core 1.
- `elm327.c` keeps exactly one command in flight (required on K-line), waits for the `>` prompt before sending, matches responses to the request, and drops late answers.
- `obd_pids.c` gives the dashboard PIDs (RPM, speed) most of the bus time; slow values (coolant, voltage, fuel) get fixed slots. Battery voltage comes from PID `0x42` or falls back to the adapter's `ATRV`.
- The link layer (`obd_link.c`) persists the selected transport and switches by stopping one stack completely (NimBLE deinit / `esp_wifi_stop`) before starting the other, so WiFi can run without modem sleep.

---

## OBD-II data & PIDs

| PID | Value | Shown on |
|-----|-------|----------|
| `0x0C` | Engine RPM | Dash (arc + value) |
| `0x0D` | Vehicle speed | Dash |
| `0x05` | Coolant temperature | Dash card, over-temp buzzer |
| `0x5C` | Oil temperature | Dash card, Live Data (if supported) |
| `0x42` / `ATRV` | Battery / module voltage | Dash card, Settings (raw value + source) |
| `0x11` | Throttle position | Live Data |
| `0x04` | Calculated engine load | Live Data |
| `0x0B` | Intake manifold pressure (MAP) | Live Data |
| `0x0F` | Intake air temperature | Live Data |
| `0x0E` | Timing advance | Live Data |
| `0x10` | Mass air flow (MAF) | Live Data |
| `0x06` / `0x07` | Short / long term fuel trim | Live Data |
| `0x14` / `0x15` | O2 sensor 1 / 2 voltage | Live Data |
| `0x2F` | Fuel level | Live Data |
| `0x01`, `03`, `07`, `02`, `04` | MIL, DTCs, freeze frame, clear | DTC screen |

Every numeric PID goes through an EMA filter with spike rejection (cold-start seeding, reset after 3 consecutive spikes). Values older than 3 s are shown as `--`.

---

## Project structure

```
├── main/
│   ├── main.c               # app_main: NVS, display, IMU, UI, OBD layers
│   ├── Kconfig.projbuild    # display + OBD adapter link options
│   ├── bsp/                 # board support: panel, LVGL port, touch, buzzer
│   ├── obd/
│   │   ├── obd_link.c/h     # BLE / WiFi transport selection
│   │   ├── ble_obd.c/h      # NimBLE central, scan/connect, GATT
│   │   ├── wifi_obd.c/h     # WiFi STA + TCP client for WiFi ELM327
│   │   ├── elm327.c/h       # ELM327 command engine and parser
│   │   ├── obd_pids.c/h     # PID discovery, polling, decoding
│   │   └── obd_dtc.c/h      # trouble codes
│   ├── data/                # vehicle data snapshot, profiles, DTC database, log
│   ├── imu/                 # QMI8658 driver and attitude
│   └── ui/                  # LVGL screens, theme, fonts (incl. Turkish glyphs)
├── simulator/               # LVGL PC simulator (Visual Studio) sharing main/ui
├── scripts/                 # round-LCD layout checker
├── docs/                    # project website (GitHub Pages), dev rules, design notes
├── CHANGELOG.md             # dated history (Turkish), current status, open items
├── partitions.csv           # NVS 24 KB · PHY 4 KB · factory app 3 MB
└── sdkconfig.defaults
```

---

## PC simulator

The UI can be developed on Windows without hardware. The simulator compiles the same `main/ui/` sources against a simulated data feed.

Requirements: Visual Studio 2022+ with **Desktop development with C++** (x64).

```powershell
cd simulator
.\build_simulator.cmd
.\Output\Binaries\Release\x64\LVGL.Simulator.exe
```

See [`simulator/README.md`](simulator/README.md).

---

## Troubleshooting

| Symptom | What to check |
|---------|---------------|
| Stuck on **SCAN** (BLE) | Adapter powered (ignition on)? Name or service UUID unusual? A phone may already be connected to it. |
| "Adapter not found" (WiFi) | Is **Settings → Link** on WiFi? Is the AP visible on a phone? If the SSID is unusual, set `OBD_WIFI_SSID`. |
| "Adapter TCP refused" | Another device holds the single TCP slot; or the adapter uses another IP/port — set `OBD_WIFI_HOST` / `OBD_WIFI_PORT`. |
| Connects but no data | Ignition must be on (engine running is best). Check the protocol line in Settings; very slow cars may need the profile's ELM timeout raised. |
| Voltage looks wrong | Settings shows the raw reading and its source (`ATRV` or `0142`); clone adapters are often off by a few tenths. |
| Display stays black | `CONFIG_BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_2_1=y` must be set; PSRAM must be detected in the boot log. |
| LVGL out of memory | PSRAM enabled and `CONFIG_LV_MEM_CUSTOM=y`. |

Serial logs (USB-JTAG port, 115200) print a link summary every 5 s: requests/s, RPM rate, timeouts, protocol.

---

## Türkçe özet

**ESP32-S3 OBD2 gösterge paneli:** Waveshare ESP32-S3-Touch-LCD-2.1 (480×480 yuvarlak dokunmatik ekran) için açık kaynak araç göstergesi. Ucuz **ELM327 klon adaptör** ile — **Bluetooth (BLE) veya WiFi** — her OBD-II araçtan canlı veri okur: devir, hız, su/yağ sıcaklığı, akü voltajı, sensör verileri, **arıza kodu (DTC) okuma ve silme** (Türkçe açıklamalı, ~190 kod), eğim göstergesi.

- **Bağlantı seçimi:** Ayarlar → **Link** karosu (BLE / WiFi). Seçilen hemen aranır, yeniden başlatma gerekmez; seçim kalıcıdır.
- **WiFi adaptörler:** `WiFi_OBDII`, `OBDII`, `V-LINK` gibi ağlar otomatik bulunur, adaptör `192.168.0.10:35000`. Adaptör tek bağlantı kabul eder — telefon uygulamasını kapatın.
- **K-line (KWP2000) araçlar** (ör. 2005 Chevrolet Kalos / Daewoo) için optimize: protokol önbelleği, tek ECU yanıt eki, RPM/hıza öncelik.
- Kurulum: hazır firmware'i [son sürümden](https://github.com/erdemerciyas/ESP32-S3-OBD2/releases/latest) indirip `0x0` adresine yazın, ya da ESP-IDF 5.3.5 ile `idf.py build` → `idf.py -p COM3 flash`. Ayrıntılı geçmiş ve açık işler: [`CHANGELOG.md`](CHANGELOG.md).

---

## Contributing

Issues and pull requests are welcome — especially reports of which ELM327 adapters (BLE or WiFi) and which cars work. When touching the connection code (`ble_obd.c`, `wifi_obd.c`, `elm327.c`), keep changes small and test on a real adapter; see [`docs/GELISTIRME_KURALLARI.md`](docs/GELISTIRME_KURALLARI.md).

## License

[MIT](LICENSE) © 2026 Erdem Erciyas. Bundled third-party code keeps its own license (LVGL simulator: MIT; managed ESP-IDF components: see their headers).

## Author

**Erdem Erciyas** — [github.com/erdemerciyas](https://github.com/erdemerciyas)

*Keywords: ESP32 OBD2, ESP32-S3 OBD-II dashboard, ELM327 BLE ESP32, ELM327 WiFi ESP32, OBD2 gauge, digital car dashboard, round LCD gauge, Waveshare ESP32-S3-Touch-LCD-2.1, LVGL car dashboard, NimBLE OBD, DTC reader, check engine code reader, K-line KWP2000, araç gösterge paneli, OBD2 arıza kodu okuyucu.*
