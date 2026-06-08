#pragma once
#include <lvgl.h>
#include <Wire.h>
#include "obd2.h"
#include "elm327.h"
#include "esp_log.h"

// ══════════════════════════════════════════════════════════════
//  Simple Color Palette
// ══════════════════════════════════════════════════════════════
#define UI_BG       lv_color_hex(0x111111)
#define UI_TEXT     lv_color_hex(0xEEEEEE)
#define UI_DIM      lv_color_hex(0x666666)
#define UI_ACCENT   lv_color_hex(0xE94560)
#define UI_GREEN    lv_color_hex(0x4CAF50)
#define UI_YELLOW   lv_color_hex(0xFFC107)
#define UI_RED      lv_color_hex(0xF44336)
#define UI_BLUE     lv_color_hex(0x42A5F5)
#define UI_CYAN     lv_color_hex(0x00BCD4)
#define UI_WHITE    lv_color_hex(0xFFFFFF)

// ══════════════════════════════════════════════════════════════
//  Widget Pointers
// ══════════════════════════════════════════════════════════════
#define MAX_GAUGES 8
static lv_obj_t* ui_arc_value[MAX_GAUGES];
static lv_obj_t* ui_arc_widget[MAX_GAUGES];
static lv_obj_t* ui_status_label = NULL;
static lv_obj_t* ui_page_container = NULL;
static lv_obj_t* ui_page_dots[4];
static uint8_t ui_current_page = 0;
#define UI_PAGE_COUNT 4

// On-screen nav buttons (created in UI_Init, persist across page switches)
static lv_obj_t* ui_btn_prev = NULL;
static lv_obj_t* ui_btn_next = NULL;

// ══════════════════════════════════════════════════════════════
//  Simple Arc Gauge
// ══════════════════════════════════════════════════════════════
static lv_obj_t* create_arc_gauge(lv_obj_t* parent, int gauge_idx,
                                   int32_t min_val, int32_t max_val,
                                   lv_color_t color,
                                   int x, int y, int size,
                                   const char* title, const char* unit) {
    int track_w = size / 10;
    if (track_w < 4) track_w = 4;
    if (track_w > 14) track_w = 14;

    lv_obj_t* arc = lv_arc_create(parent);
    lv_obj_set_size(arc, size, size);
    lv_obj_set_pos(arc, x, y);
    lv_arc_set_bg_angles(arc, 135, 45);
    lv_arc_set_range(arc, min_val, max_val);
    lv_arc_set_value(arc, min_val);

    lv_obj_set_style_arc_color(arc, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, track_w, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, track_w, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);

    const lv_font_t* val_font;
    if (size >= 180)      val_font = &lv_font_montserrat_48;
    else if (size >= 140) val_font = &lv_font_montserrat_36;
    else if (size >= 110) val_font = &lv_font_montserrat_24;
    else                  val_font = &lv_font_montserrat_18;

    lv_obj_t* val_lbl = lv_label_create(arc);
    lv_label_set_text(val_lbl, "---");
    lv_obj_set_style_text_color(val_lbl, UI_WHITE, 0);
    lv_obj_set_style_text_font(val_lbl, val_font, 0);
    lv_obj_center(val_lbl);

    if (gauge_idx >= 0 && gauge_idx < MAX_GAUGES) {
        ui_arc_value[gauge_idx] = val_lbl;
        ui_arc_widget[gauge_idx] = arc;
    }

    lv_obj_t* unit_lbl = lv_label_create(arc);
    lv_label_set_text(unit_lbl, unit);
    lv_obj_set_style_text_color(unit_lbl, UI_DIM, 0);
    lv_obj_set_style_text_font(unit_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(unit_lbl, LV_ALIGN_BOTTOM_MID, 0, -(size / 6));

    lv_obj_t* title_lbl = lv_label_create(parent);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_color(title_lbl, color, 0);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(title_lbl, x + (size / 2) - (strlen(title) * 4), y + size + 4);

    return arc;
}

// ══════════════════════════════════════════════════════════════
//  Page 0: Dashboard — RPM + Speed + 3 gauges
// ══════════════════════════════════════════════════════════════
static void create_dashboard_page(lv_obj_t* parent) {
    int big = 215, gap = 30;
    int x0 = (480 - 2 * big - gap) / 2;

    create_arc_gauge(parent, 0, 0, 8000, UI_ACCENT, x0, 5, big, "RPM", "rpm");
    create_arc_gauge(parent, 1, 0, 240, UI_WHITE, x0 + big + gap, 5, big, "SPEED", "km/h");

    int sat = 142, sat_gap = 10;
    int sx = (480 - 3 * sat - 2 * sat_gap) / 2;
    int sy = big + 32;

    create_arc_gauge(parent, 2, -40, 130, UI_CYAN, sx, sy, sat, "COOLANT", "C");
    create_arc_gauge(parent, 5, 0, 100, UI_GREEN, sx + sat + sat_gap, sy, sat, "THROTTLE", "%");
    create_arc_gauge(parent, 4, 0, 100, UI_BLUE, sx + 2 * (sat + sat_gap), sy, sat, "LOAD", "%");
}

// ══════════════════════════════════════════════════════════════
//  Page 1: Engine Data (2x3 grid)
// ══════════════════════════════════════════════════════════════
static void create_engine_page(lv_obj_t* parent) {
    int y0 = 10, gsize = 130, gap = 12;
    int x0 = (480 - (3 * gsize + 2 * gap)) / 2;
    int dy = gsize + 34;

    create_arc_gauge(parent, 3, -40, 80, UI_CYAN, x0, y0, gsize, "INTAKE", "C");
    create_arc_gauge(parent, 6, 0, 255, UI_YELLOW, x0 + gsize + gap, y0, gsize, "MAP", "kPa");
    create_arc_gauge(parent, 8, -25, 25, UI_GREEN, x0 + 2 * (gsize + gap), y0, gsize, "STFT", "%");
    create_arc_gauge(parent, 10, 0, 655, UI_ACCENT, x0, y0 + dy, gsize, "MAF", "g/s");
    create_arc_gauge(parent, 11, -64, 64, UI_BLUE, x0 + gsize + gap, y0 + dy, gsize, "TIMING", "deg");
    create_arc_gauge(parent, 13, 0, 200, UI_GREEN, x0 + 2 * (gsize + gap), y0 + dy, gsize, "ECU", "Vx10");
}

// ══════════════════════════════════════════════════════════════
//  Page 2: Vehicle Data (2x3 grid)
// ══════════════════════════════════════════════════════════════
static void create_vehicle_page(lv_obj_t* parent) {
    int y0 = 10, gsize = 130, gap = 12;
    int x0 = (480 - (3 * gsize + 2 * gap)) / 2;
    int dy = gsize + 34;

    create_arc_gauge(parent, 12, 0, 100, UI_YELLOW, x0, y0, gsize, "FUEL", "%");
    create_arc_gauge(parent, 14, -40, 215, UI_YELLOW, x0 + gsize + gap, y0, gsize, "OIL", "C");
    create_arc_gauge(parent, 16, -40, 60, UI_CYAN, x0 + 2 * (gsize + gap), y0, gsize, "AMBIENT", "C");
    create_arc_gauge(parent, 15, 0, 200, UI_GREEN, x0, y0 + dy, gsize, "FUEL RT", "L/h");
    create_arc_gauge(parent, 19, 0, 3600, UI_BLUE, x0 + gsize + gap, y0 + dy, gsize, "RUNTIME", "s");
    create_arc_gauge(parent, 17, 0, 255, UI_BLUE, x0 + 2 * (gsize + gap), y0 + dy, gsize, "BARO", "kPa");
}

// ══════════════════════════════════════════════════════════════
//  Page 3: Debug Info
// ══════════════════════════════════════════════════════════════
static void create_info_page(lv_obj_t* parent) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, 460, 430);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    const char* lines[] = {
        "ESP32-S3 OBD2 WiFi Gauge",
        "",
        "WiFi: Auto-detect ELM327 SSID",
        "Auth: Open (no password)",
        "Protocol: Auto (ATSP0)",
        "",
        "PIDs: 20 standard OBD2",
        "Poll: ~200ms per PID",
        "Reconnect: infinite retry",
        "",
        "Swipe: change page",
        "Web UI: see serial log",
        "",
        "Waveshare Touch-LCD-2.1"
    };

    int y = 10;
    for (int i = 0; i < 14; i++) {
        if (strlen(lines[i]) == 0) { y += 10; continue; }
        lv_obj_t* lbl = lv_label_create(card);
        lv_label_set_text(lbl, lines[i]);
        lv_obj_set_style_text_color(lbl, (i == 0) ? UI_ACCENT : UI_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(lbl, 10, y);
        y += 24;
    }
}

// ══════════════════════════════════════════════════════════════
//  Page Dots
// ══════════════════════════════════════════════════════════════
static void update_page_dots() {
    int dot_w = 8, dot_gap = 12;
    int active_w = 20;
    int total = 0;
    int widths[4];
    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        widths[i] = (i == ui_current_page) ? active_w : dot_w;
        total += widths[i];
    }
    total += (UI_PAGE_COUNT - 1) * dot_gap;
    int cx = (480 - total) / 2;
    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        bool active = (i == ui_current_page);
        lv_obj_set_size(ui_page_dots[i], widths[i], dot_w);
        lv_obj_set_pos(ui_page_dots[i], cx, 472);
        lv_obj_set_style_bg_color(ui_page_dots[i], active ? UI_ACCENT : UI_DIM, 0);
        lv_obj_set_style_bg_opa(ui_page_dots[i], active ? LV_OPA_COVER : LV_OPA_40, 0);
        cx += widths[i] + dot_gap;
    }
}

// ══════════════════════════════════════════════════════════════
//  UI Init
// ══════════════════════════════════════════════════════════════
inline void UI_Init() {
    lv_obj_set_style_bg_color(lv_scr_act(), UI_BG, 0);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);

    // Status bar
    lv_obj_t* sbar = lv_obj_create(lv_scr_act());
    lv_obj_set_size(sbar, 480, 24);
    lv_obj_set_pos(sbar, 0, 0);
    lv_obj_set_style_bg_color(sbar, lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_opa(sbar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sbar, 0, 0);
    lv_obj_set_style_radius(sbar, 0, 0);
    lv_obj_set_style_pad_all(sbar, 2, 0);
    lv_obj_clear_flag(sbar, LV_OBJ_FLAG_SCROLLABLE);

    ui_status_label = lv_label_create(sbar);
    lv_label_set_text(ui_status_label, "Starting...");
    lv_obj_set_style_text_color(ui_status_label, UI_TEXT, 0);
    lv_obj_set_style_text_font(ui_status_label, &lv_font_montserrat_12, 0);
    lv_obj_align(ui_status_label, LV_ALIGN_LEFT_MID, 8, 0);

    // Page container
    ui_page_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ui_page_container, 480, 446);
    lv_obj_set_pos(ui_page_container, 0, 24);
    lv_obj_set_style_bg_opa(ui_page_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui_page_container, 0, 0);
    lv_obj_set_style_pad_all(ui_page_container, 0, 0);
    lv_obj_clear_flag(ui_page_container, LV_OBJ_FLAG_SCROLLABLE);

    // Page dots
    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        ui_page_dots[i] = lv_obj_create(lv_scr_act());
        lv_obj_set_size(ui_page_dots[i], 8, 8);
        lv_obj_set_style_radius(ui_page_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(ui_page_dots[i], 0, 0);
        lv_obj_clear_flag(ui_page_dots[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    for (int i = 0; i < MAX_GAUGES; i++) {
        ui_arc_value[i] = NULL;
        ui_arc_widget[i] = NULL;
    }

    create_dashboard_page(ui_page_container);
    update_page_dots();

    // ── On-screen nav buttons (visual only — UI_HandleTouch() does the hit-test) ──
    ui_btn_prev = lv_btn_create(lv_scr_act());
    lv_obj_set_size(ui_btn_prev, 60, 60);
    lv_obj_set_pos(ui_btn_prev, 10, 410);
    lv_obj_set_style_bg_color(ui_btn_prev, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(ui_btn_prev, LV_OPA_60, 0);
    lv_obj_set_style_radius(ui_btn_prev, 30, 0);
    lv_obj_set_style_border_color(ui_btn_prev, UI_DIM, 0);
    lv_obj_set_style_border_width(ui_btn_prev, 1, 0);
    lv_obj_t* lbl_p = lv_label_create(ui_btn_prev);
    lv_label_set_text(lbl_p, "<");
    lv_obj_set_style_text_color(lbl_p, UI_WHITE, 0);
    lv_obj_set_style_text_font(lbl_p, &lv_font_montserrat_24, 0);
    lv_obj_center(lbl_p);

    ui_btn_next = lv_btn_create(lv_scr_act());
    lv_obj_set_size(ui_btn_next, 60, 60);
    lv_obj_set_pos(ui_btn_next, 410, 410);
    lv_obj_set_style_bg_color(ui_btn_next, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(ui_btn_next, LV_OPA_60, 0);
    lv_obj_set_style_radius(ui_btn_next, 30, 0);
    lv_obj_set_style_border_color(ui_btn_next, UI_DIM, 0);
    lv_obj_set_style_border_width(ui_btn_next, 1, 0);
    lv_obj_t* lbl_n = lv_label_create(ui_btn_next);
    lv_label_set_text(lbl_n, ">");
    lv_obj_set_style_text_color(lbl_n, UI_WHITE, 0);
    lv_obj_set_style_text_font(lbl_n, &lv_font_montserrat_24, 0);
    lv_obj_center(lbl_n);
}

// ══════════════════════════════════════════════════════════════
//  Page Switch
// ══════════════════════════════════════════════════════════════
inline void UI_SwitchPage(uint8_t page) {
    if (page >= UI_PAGE_COUNT) page = 0;
    ui_current_page = page;
    lv_obj_clean(ui_page_container);
    for (int i = 0; i < MAX_GAUGES; i++) {
        ui_arc_value[i] = NULL;
        ui_arc_widget[i] = NULL;
    }
    switch (page) {
        case 0: create_dashboard_page(ui_page_container); break;
        case 1: create_engine_page(ui_page_container); break;
        case 2: create_vehicle_page(ui_page_container); break;
        case 3: create_info_page(ui_page_container); break;
    }
    update_page_dots();
}

// ══════════════════════════════════════════════════════════════
//  Update Helpers
// ══════════════════════════════════════════════════════════════
inline void UI_UpdateArc(int idx, float value, const char* fmt = "%.0f") {
    if (idx < 0 || idx >= MAX_GAUGES) return;
    if (!ui_arc_widget[idx] || !ui_arc_value[idx]) return;
    lv_arc_set_value(ui_arc_widget[idx], (int32_t)value);
    char buf[16];
    snprintf(buf, sizeof(buf), fmt, value);
    lv_label_set_text(ui_arc_value[idx], buf);
}

inline void UI_UpdateSpeed(float speed) {
    UI_UpdateArc(1, speed, "%.0f");
}

inline void UI_SetStatus(const char* text) {
    if (!ui_status_label) return;
    lv_label_set_text(ui_status_label, text);
}

// ══════════════════════════════════════════════════════════════
//  Touch Handler (CST820 coordinate-based, with on-screen buttons)
// ══════════════════════════════════════════════════════════════
static const uint8_t CST820_ADDR = 0x15;          // CST820 I2C address
static const uint8_t CST820_ADDR_ALT = 0x5A;      // some clones
static const uint8_t REG_TOUCH_NUM = 0x02;        // touch count (bits 0-1)
static const uint8_t REG_XPOS_H    = 0x03;
static const uint8_t REG_XPOS_L    = 0x04;
static const uint8_t REG_YPOS_H    = 0x05;
static const uint8_t REG_YPOS_L    = 0x06;
static const uint8_t REG_DIS_AUTO  = 0xFE;        // 0x01 = keep awake
static const uint8_t REG_IRQ_CTL   = 0xFA;        // 0x00 = continuous
static const uint8_t REG_CHIP_ID   = 0xA3;        // expect 0xB6
static uint8_t cst820_addr_used = 0x15;
static bool     touch_available = false;
static bool     touch_was_pressed = false;
static uint16_t touch_down_x = 0, touch_down_y = 0;
static uint32_t last_touch_ms = 0;

static inline void CST820_WriteReg(uint8_t addr, uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

static inline uint8_t CST820_ReadReg(uint8_t addr, uint8_t reg) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return 0xFF;
    Wire.requestFrom((int)addr, (int)1);
    return Wire.available() ? Wire.read() : 0xFF;
}

static inline bool CST820_ReadBlock(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t len) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    Wire.requestFrom((int)addr, (int)len);
    for (uint8_t i = 0; i < len && Wire.available(); i++) buf[i] = Wire.read();
    return true;
}

static inline void UI_InitTouch() {
    pinMode(TOUCH_INT_PIN, INPUT_PULLUP);

    // Scan I2C for CST820
    Wire.beginTransmission(CST820_ADDR);
    if (Wire.endTransmission() == 0) {
        cst820_addr_used = CST820_ADDR;
        touch_available = true;
    } else {
        Wire.beginTransmission(CST820_ADDR_ALT);
        if (Wire.endTransmission() == 0) {
            cst820_addr_used = CST820_ADDR_ALT;
            touch_available = true;
        }
    }

    if (!touch_available) {
        Serial.println("[Touch] CST820 NOT FOUND on I2C bus");
        return;
    }
    Serial.printf("[Touch] CST820 found @ 0x%02X\n", cst820_addr_used);

    // Verify chip ID
    uint8_t chip = CST820_ReadReg(cst820_addr_used, REG_CHIP_ID);
    Serial.printf("[Touch] Chip ID: 0x%02X (expect 0xB6)\n", chip);

    // Configure for reliable polling
    delay(30);
    CST820_WriteReg(cst820_addr_used, REG_DIS_AUTO, 0x01);  // disable auto-sleep
    CST820_WriteReg(cst820_addr_used, REG_IRQ_CTL,  0x00);  // continuous IRQ
    delay(10);
    Serial.println("[Touch] CST820 ready (coordinate mode)");
}

static inline bool CST820_GetTouch(uint16_t* x, uint16_t* y) {
    if (!touch_available) return false;
    uint8_t buf[5];
    if (!CST820_ReadBlock(cst820_addr_used, REG_TOUCH_NUM, buf, 5)) return false;
    uint8_t n = buf[0] & 0x03;
    if (n == 0) return false;
    // CST820 12-bit coords in 0..4095; map to 0..479
    uint16_t rx = ((buf[1] & 0x0F) << 8) | buf[2];
    uint16_t ry = ((buf[3] & 0x0F) << 8) | buf[4];
    *x = (rx * 480UL) / 4096;
    *y = (ry * 480UL) / 4096;
    return true;
}

static inline void UI_SwitchNext() {
    UI_SwitchPage((ui_current_page + 1) % UI_PAGE_COUNT);
}
static inline void UI_SwitchPrev() {
    UI_SwitchPage(ui_current_page == 0 ? UI_PAGE_COUNT - 1 : ui_current_page - 1);
}

static inline void UI_HandleTouch() {
    if (!touch_available) return;
    static uint32_t last_poll_ms = 0;
    if (millis() - last_poll_ms < 30) return;
    last_poll_ms = millis();

    uint16_t x, y;
    bool pressed = CST820_GetTouch(&x, &y);

    if (pressed && !touch_was_pressed) {
        // Touch-down: record start point
        touch_down_x = x;
        touch_down_y = y;
        last_touch_ms = millis();
    } else if (!pressed && touch_was_pressed) {
        // Touch-up: detect tap (short, small movement)
        uint32_t dur = millis() - last_touch_ms;
        int16_t dx = (int16_t)x - (int16_t)touch_down_x;
        int16_t dy = (int16_t)y - (int16_t)touch_down_y;
        uint32_t dist2 = (uint32_t)(dx * dx + dy * dy);

        if (dur < 500 && dist2 < 60 * 60) {
            // It's a tap
            Serial.printf("[Touch] Tap at (%u,%u)\n", x, y);
            // On-screen prev button (x=10..70, y=410..470)
            if (x >= 10 && x <= 70 && y >= 410 && y <= 470) {
                UI_SwitchPrev();
            }
            // On-screen next button (x=410..470, y=410..470)
            else if (x >= 410 && x <= 470 && y >= 410 && y <= 470) {
                UI_SwitchNext();
            }
            // Left edge -> prev page
            else if (x < 60) {
                UI_SwitchPrev();
            }
            // Right edge -> next page
            else if (x > 420) {
                UI_SwitchNext();
            }
        }
    }
    touch_was_pressed = pressed;
}
