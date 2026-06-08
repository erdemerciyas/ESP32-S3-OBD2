# ESP32-S3 OBD2 WiFi Gauge - Project Plan

## Objective
Universal OBD2 gauge using **Waveshare ESP32-S3-Touch-LCD-2.1** + **ELM327 WiFi** adapter.
Reads CAN bus data via WiFi TCP and displays real-time metrics on 480x480 LCD with LVGL.

---

## Phase 1: Project Setup ✅ COMPLETE
- [x] `platformio.ini` - ESP32-S3 board, espressif32@6.9.0, Arduino, PSRAM enabled, 16MB flash
- [x] `include/config.h` - WiFi SSID=OBDII, no password (open network), IP=192.168.0.10, port=35000
- [x] `include/pin_config.h` - All GPIO definitions (I2C, SPI, RGB, touch, backlight)

## Phase 2: Hardware Drivers ✅ COMPLETE
- [x] `include/TCA9554PWR.h` - I2C GPIO expander (LCD reset, CS, touch reset, power)
- [x] `include/display.h` - ST7701 SPI init + RGB parallel panel + LVGL v8.4 flush callback
- [x] TCA9554PWR pin mapping verified: PIN1=LCD_RST, PIN2=TOUCH_RST, PIN3=LCD_CS, PIN8=LCD_PWR
- [x] Backlight PWM: GPIO6, 20kHz, 10-bit (ledcSetup + ledcAttachPin API)
- [x] Fixed: ledcAttach → ledcSetup/ledcAttachPin (Arduino core 3.x)
- [x] Fixed: LCD_CLK_SRC_DEFAULT → LCD_CLK_SRC_PLL160M
- [x] Fixed: Removed unsupported rgb_panel_config members (bits_per_pixel, num_fbs, etc.)

## Phase 3: WiFi + ELM327 Communication ✅ COMPLETE
- [x] `include/elm327.h` - WiFi STA + TCP client (192.168.0.10:35000)
- [x] AT command sequence: ATZ→ATE0→ATL0→ATS0→ATH0→ATSP0→ATAT2→ATST32
- [x] PID request/response parser (Mode 01)
- [x] Auto-reconnect with retry logic (max 10 attempts, 5s interval)
- [x] WiFi network scan utility (`scanAndPrintNetworks()`)
- [x] ELM327 connects to open WiFi (no password)

## Phase 4: OBD2 Protocol Layer ✅ COMPLETE
- [x] `include/obd2.h` - 20 universal PIDs (RPM, speed, temps, load, throttle, MAF, fuel trims, etc.)
- [x] Hex-to-float decoders per PID formula (RPM=A*256+B/4, Speed=A, Temp=A-40, etc.)
- [x] OBD2Data struct for value storage + validity tracking

## Phase 5: UI / Dashboard (LVGL v8.4) ✅ GAUGE REDESIGN COMPLETE
- [x] `include/ui.h` - Professional dark automotive theme (480x480)
- [x] **4-page layout** with circular arc gauges (lv_arc) for all OBD2 PIDs
- [x] Page 0: **Dashboard** - Large RPM arc (260px, 0-8000), digital speed (48px font), 3 arc gauges (Coolant/Throttle/Load)
- [x] Page 1: **Engine** - 6 arc gauges in 2x3 grid: Intake, MAP, STFT, MAF, Timing, ECU Voltage
- [x] Page 2: **Vehicle** - 6 arc gauges: Fuel Level, Oil Temp, Ambient, Fuel Rate, Run Time, Barometric
- [x] Page 3: **Connection Info** - WiFi details, protocol, PID count, swipe instructions
- [x] Touch swipe page switching (CST820 I2C gesture detection)
- [x] Status bar with connection state + page indicator dots (4 dots)
- [x] OBD2 standard ranges defined for all 20 PIDs
- [x] Color scheme: dark navy #0D1117, accent red #E94560, cyan #00BCD4, green #00E676, blue #58A6FF
- [x] All 20 OBD2 PIDs now polled in round-robin (priority-ordered)

## Phase 6: Main Application ✅ COMPLETE
- [x] `src/main.cpp` - Boot sequence: I2C → TCA9554PWR → Display → LVGL → UI → ELM327
- [x] FreeRTOS Task 1 (Core 1): OBD2 PID round-robin polling, ~30ms per PID
- [x] FreeRTOS Task 2 (Core 0): LVGL render + UI updates (100ms) + touch handling
- [x] Connection loss detection and ESP.restart() after max retries
- [x] `include/lv_conf.h` - LVGL v8.4 config (16-bit, custom malloc in PSRAM, fonts, widgets)
- [x] Fixed: LV_MEM_CUSTOM=1 to use stdlib malloc (eliminates 256KB DRAM pool)

## Phase 7: Build & Test ✅ BUILD SUCCESS
- [x] All source files created and compilation errors fixed
- [x] DRAM overflow fixed (PSRAM config + LVGL custom malloc)
- [x] **Build #3: SUCCESS** — RAM 14.6% (47KB/320KB), Flash 35.1% (1.1MB/3MB)
- [ ] Flash firmware to ESP32-S3-Touch-LCD-2.1
- [ ] Test display init (blank screen = SPI/RGB config issue)
- [ ] Test WiFi scan to verify ELM327 SSID discovery
- [ ] Test TCP connection to ELM327 (192.168.0.10:35000)
- [ ] Test OBD2 PID reading with engine ON
- [ ] Tune UI refresh and touch responsiveness

---

## Build Issues Resolved

| # | Error | Fix |
|---|-------|-----|
| 1 | `ledcAttach` not declared | Use `ledcSetup()` + `ledcAttachPin()` (Arduino core 3.x API) |
| 2 | `LCD_CLK_SRC_DEFAULT` not declared | Changed to `LCD_CLK_SRC_PLL160M` |
| 3 | `esp_lcd_rgb_panel_config_t` missing members | Removed bits_per_pixel, num_fbs, bounce_buffer_size_px, double_fb |
| 4 | `lv_disp_register` not declared | Changed to `lv_disp_drv_register` |
| 5 | DRAM overflow (22800 bytes) | PSRAM enabled + `LV_MEM_CUSTOM=1` with stdlib malloc |

---

## Critical Dependencies

| Component | Version | Purpose |
|-----------|---------|--------|
| ESP32 Arduino Core | espressif32@6.9.0 | MCU framework |
| LVGL | v8.4.0 (^8.3.11) | GUI library |
| Wire | built-in | I2C for TCA9554PWR + CST820 |
| WiFi | built-in | STA mode to ELM327 AP (open, no password) |
| esp_lcd | built-in | RGB panel driver |

## Known Pitfalls

1. **LVGL v9.x incompatible** - must use v8.3.x/v8.4.x
2. **TCA9554PWR must init first** - LCD power/reset/CS via I2C expander
3. **ST7701 = SPI for config + RGB for pixels** - two separate interfaces
4. **ELM327 needs `\r` only** - no `\n` in commands
5. **ATSP0 critical** - auto-protocol for universal compatibility
6. **LVGL buffers in PSRAM** - use custom malloc, framebuffer in PSRAM
7. **Engine must be ON** - ignition alone won't return OBD2 data
8. **WiFi SSID varies** - adapters use OBDII, V-LINKED, WIFI_OBD2 etc.
9. **DRAM is limited** - never allocate large buffers statically, use PSRAM
10. **Arduino core 3.x LEDC API** - ledcSetup + ledcAttachPin, not ledcAttach

## File Structure

```
esp-obd/
├── src/main.cpp              ← Entry point, FreeRTOS tasks
├── include/
│   ├── config.h              ← WiFi, ELM327, polling config
│   ├── pin_config.h          ← GPIO pin definitions
│   ├── TCA9554PWR.h          ← I2C GPIO expander driver
│   ├── display.h             ← ST7701 + RGB panel + LVGL driver
│   ├── elm327.h              ← WiFi TCP + AT command client
│   ├── obd2.h                ← OBD2 PID definitions + decoders
│   ├── ui.h                  ← LVGL dashboard (3 pages)
│   └── lv_conf.h             ← LVGL v8.4 configuration
├── platformio.ini            ← PlatformIO build config
└── TODO.md                   ← This file
```
