---
name: automotive-gauge
description: >-
  ESP32 OBD dashboard gauge patterns for LVGL 8 (lv_meter). Use when building
  or refining RPM/speed/throttle gauges on Waveshare 480x480 round touch LCD.
---

# Automotive Gauge (ESP-ARD)

Design direction: **Data Dashboard** — high contrast, scannable values, dark palette. **Round display**: keep widgets inside `LCD_SAFE_*` in `config.h`; never place wide bars at square corners (they clip).

## Workflow

1. Read `components.md` for gauge-specific rules.
2. Match existing colors in `dashboard_ui.cpp` / `wifi_menu_ui.cpp`.
3. Prefer `lv_meter` (needle + color zones) over plain `lv_arc` for primary RPM.
4. Use `RpmGaugeUi` in `gauge_ui.h` — do not duplicate meter setup in dashboard.
5. Keep gauges non-interactive (`LV_OBJ_FLAG_CLICKABLE` cleared); touch layer stays on top.

## Quick reference

| Component | Widget | Notes |
|-----------|--------|-------|
| Primary RPM | `lv_meter` | Scale 0–8 (×1000), 270° sweep, rotation 135° |
| Zone arcs | `lv_meter_add_arc` | Green / amber / red static bands |
| Needle | `lv_meter_add_needle_line` | White, width 5 |
| Secondary throttle | `lv_bar` | 0–100 %, rounded, accent color |
| Status | `lv_label` | Top bar, 12–14px Montserrat |

## Anti-patterns

- Generic purple gradients or light themes on this hardware.
- Clickable gauges stealing double-tap for WiFi menu.
- More than one heavy meter on screen (ESP32 draw budget).
- RPM labels every 500 — use major ticks at ×1000 only.
