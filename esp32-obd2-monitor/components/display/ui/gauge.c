#include "gauge.h"
#include "styles.h"
#include "ui_fonts.h"
#include "board_config.h"
#include "app.h"
#include "settings.h"
#include "obd_service.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "gauge";

typedef struct {
    const char *label;
    const char *unit;
    int16_t default_max;
} gauge_meta_t;

static const gauge_meta_t gauge_meta[GAUGE_MAX] = {
    {"RPM", "rpm", 6500},
    {"SPEED", "km/h", 180},
    {"COOLANT", "C", 120},
    {"VOLT", "V", 16},
    {"THROTTLE", "%", 100},
    {"FUEL", "%", 100},
    {"LOAD", "%", 100},
    {"INTAKE", "C", 80},
    {"L/100", "L/h", 25},
    {"DTC", "CODE", 10},
};

extern app_settings_t g_settings;

static lv_obj_t *fullscreen_gauge_container;
static lv_obj_t *gauge_face;
static lv_obj_t *fullscreen_arc_indicator;
static lv_obj_t *info_panel;
static lv_obj_t *fullscreen_label_name;
static lv_obj_t *fullscreen_label_value;
static lv_obj_t *fullscreen_label_unit;

static gauge_type_t current_fullscreen_gauge = GAUGE_RPM;
static gauge_type_t pending_gauge = GAUGE_RPM;
static int16_t fullscreen_gauge_values[GAUGE_MAX] = {0};
static int16_t gauge_target_values[GAUGE_MAX] = {0};
static bool gauge_value_valid[GAUGE_MAX] = {false};
static bool gauge_nav_available[GAUGE_MAX] = {true};
static lv_obj_t *indicator_dots[GAUGE_MAX];
static lv_obj_t *indicator_index_label;
static char value_text_buf[16];
static bool gauge_transition_busy;
static int16_t last_drawn_value = -1;
static uint16_t last_drawn_arc = 0xFFFF;
static lv_color_t last_drawn_arc_color;
static lv_color_t last_drawn_value_color;

#define GAUGE_ARC_MARGIN         UI_ROUND_INSET
#define GAUGE_ARC_SIZE           (UI_SCREEN_W - (GAUGE_ARC_MARGIN * 2))
#define GAUGE_ARC_STROKE_TRACK   12
#define GAUGE_ARC_STROKE_VALUE   14
#define GAUGE_INFO_W             (GAUGE_ARC_SIZE - 48)
#define GAUGE_INFO_H             280
#define GAUGE_VALUE_FONT         (&font_gauge_96)
#define GAUGE_FADE_OUT_MS        160
#define GAUGE_FADE_IN_MS         220

static void prepare_obj(lv_obj_t *obj)
{
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}

const char *gauge_get_label(gauge_type_t type)
{
    if (type >= GAUGE_MAX) {
        return "?";
    }
    return gauge_meta[type].label;
}

uint32_t gauge_get_color(gauge_type_t type)
{
    if (type >= GAUGE_MAX) {
        return 0xF08A1C;
    }
    return g_settings.gauge_colors[type];
}

static int16_t gauge_max_value(gauge_type_t type)
{
    int16_t max_val;
    if (type == GAUGE_RPM) {
        max_val = (int16_t)g_settings.max_rpm;
    } else if (type == GAUGE_SPEED) {
        max_val = (int16_t)g_settings.max_speed;
    } else if (type >= GAUGE_MAX) {
        return 1;
    } else {
        max_val = gauge_meta[type].default_max;
    }
    return max_val > 0 ? max_val : 1;
}

static int gauge_order_index(gauge_type_t type)
{
    for (int i = 0; i < GAUGE_MAX; i++) {
        if (g_settings.gauge_order[i] == (uint8_t)type) {
            return i;
        }
    }
    return (int)type;
}

static void apply_gauge_content(gauge_type_t type);

void gauge_swap_order_slots(int slot_a, int slot_b)
{
    if (slot_a < 0 || slot_b < 0 || slot_a >= GAUGE_MAX || slot_b >= GAUGE_MAX) {
        return;
    }
    const uint8_t tmp = g_settings.gauge_order[slot_a];
    g_settings.gauge_order[slot_a] = g_settings.gauge_order[slot_b];
    g_settings.gauge_order[slot_b] = tmp;
}

void gauge_settings_changed(void)
{
    last_drawn_value = -1;
    last_drawn_arc = 0xFFFF;
    apply_gauge_content(current_fullscreen_gauge);
    gauge_update_indicator_row(current_fullscreen_gauge);
}

static uint16_t calc_arc_value(gauge_type_t type)
{
    const int16_t max_val = gauge_max_value(type);
    int16_t clamped = fullscreen_gauge_values[type];
    if (clamped < 0) {
        clamped = 0;
    }
    if (clamped > max_val) {
        clamped = max_val;
    }
    if (max_val <= 0) {
        return 0;
    }
    return (uint16_t)((float)clamped / max_val * 1000);
}

static lv_color_t calc_arc_color(gauge_type_t type)
{
    const int16_t max_val = gauge_max_value(type);
    int16_t clamped = fullscreen_gauge_values[type];
    if (clamped < 0) {
        clamped = 0;
    }
    if (clamped > max_val) {
        clamped = max_val;
    }
    if (max_val <= 0) {
        return lv_color_hex(gauge_get_color(type));
    }
    const float ratio = (float)clamped / max_val;
    if (ratio < 0.6f) {
        return lv_color_hex(gauge_get_color(type));
    }
    if (ratio < 0.85f) {
        return color_warning;
    }
    return color_danger;
}

static lv_color_t calc_value_color(gauge_type_t type)
{
    const int16_t max_val = gauge_max_value(type);
    int16_t clamped = fullscreen_gauge_values[type];
    if (clamped < 0) {
        clamped = 0;
    }
    if (clamped > max_val) {
        clamped = max_val;
    }
    if (max_val <= 0) {
        return lv_color_hex(gauge_get_color(type));
    }
    const float ratio = (float)clamped / max_val;
    return (ratio >= 0.85f) ? color_danger : lv_color_hex(gauge_get_color(type));
}

static void anim_opa_exec(void *obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void apply_gauge_content(gauge_type_t type)
{
    const int16_t value = fullscreen_gauge_values[type];
    const uint32_t gauge_color = gauge_get_color(type);

    lv_label_set_text(fullscreen_label_name, gauge_meta[type].label);
    lv_obj_set_style_text_color(fullscreen_label_name, lv_color_hex(gauge_color), 0);
    lv_label_set_text(fullscreen_label_unit, gauge_meta[type].unit);
    lv_obj_set_style_text_color(fullscreen_label_unit, lv_color_hex(gauge_color), 0);
    lv_obj_set_style_opa(fullscreen_label_unit, LV_OPA_70, 0);

    if (!gauge_value_valid[type]) {
        lv_label_set_text(fullscreen_label_value, "--");
        lv_obj_set_style_text_color(fullscreen_label_value, color_text_dim, 0);
        lv_obj_set_style_arc_color(fullscreen_arc_indicator, color_card_border, LV_PART_INDICATOR);
        lv_arc_set_value(fullscreen_arc_indicator, 0);
        return;
    }

    if (type == GAUGE_VOLTAGE) {
        snprintf(value_text_buf, sizeof(value_text_buf), "%.1f", value / 10.0f);
    } else if (type == GAUGE_FUEL_CONSUMPTION) {
        snprintf(value_text_buf, sizeof(value_text_buf), "%.1f", value / 10.0f);
    } else {
        snprintf(value_text_buf, sizeof(value_text_buf), "%d", value);
    }
    lv_label_set_text(fullscreen_label_value, value_text_buf);
    lv_obj_set_style_text_color(fullscreen_label_value, calc_value_color(type), 0);

    lv_obj_set_style_arc_color(fullscreen_arc_indicator, calc_arc_color(type), LV_PART_INDICATOR);
    lv_arc_set_value(fullscreen_arc_indicator, calc_arc_value(type));
}

static void transition_fade_in_done(lv_anim_t *a)
{
    (void)a;
    gauge_transition_busy = false;
}

static void transition_fade_in_start(lv_anim_t *a)
{
    (void)a;
    current_fullscreen_gauge = pending_gauge;
    last_drawn_value = -1;
    last_drawn_arc = 0xFFFF;
    apply_gauge_content(pending_gauge);
    last_drawn_value = fullscreen_gauge_values[pending_gauge];
    last_drawn_arc = calc_arc_value(pending_gauge);
    last_drawn_arc_color = calc_arc_color(pending_gauge);
    last_drawn_value_color = calc_value_color(pending_gauge);

    lv_anim_t fade_in;
    lv_anim_init(&fade_in);
    lv_anim_set_var(&fade_in, gauge_face);
    lv_anim_set_values(&fade_in, LV_OPA_10, LV_OPA_COVER);
    lv_anim_set_duration(&fade_in, GAUGE_FADE_IN_MS);
    lv_anim_set_path_cb(&fade_in, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&fade_in, anim_opa_exec);
    lv_anim_set_completed_cb(&fade_in, transition_fade_in_done);
    lv_anim_start(&fade_in);
}

static void transition_fade_out_done(lv_anim_t *a)
{
    (void)a;
    transition_fade_in_start(NULL);
}

static void start_soft_transition(gauge_type_t type)
{
    pending_gauge = type;
    gauge_transition_busy = true;

    lv_anim_delete(gauge_face, anim_opa_exec);

    lv_anim_t fade_out;
    lv_anim_init(&fade_out);
    lv_anim_set_var(&fade_out, gauge_face);
    lv_anim_set_values(&fade_out, LV_OPA_COVER, LV_OPA_10);
    lv_anim_set_duration(&fade_out, GAUGE_FADE_OUT_MS);
    lv_anim_set_path_cb(&fade_out, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&fade_out, anim_opa_exec);
    lv_anim_set_completed_cb(&fade_out, transition_fade_out_done);
    lv_anim_start(&fade_out);
}

void gauge_create_widget(lv_obj_t *parent, gauge_type_t type, int16_t x, int16_t y)
{
    (void)parent;
    (void)type;
    (void)x;
    (void)y;
}

void gauge_set_value(gauge_type_t type, int16_t value)
{
    if (type < GAUGE_MAX) {
        fullscreen_gauge_values[type] = value;
    }
}

int16_t gauge_get_value(gauge_type_t type)
{
    if (type >= GAUGE_MAX) {
        return 0;
    }
    return fullscreen_gauge_values[type];
}

void gauge_create_fullscreen(lv_obj_t *parent, gauge_type_t type)
{
    ESP_LOGI(TAG, "Gauge UI arc=%d (unified fade transition)", GAUGE_ARC_SIZE);

    fullscreen_gauge_container = lv_obj_create(parent);
    lv_obj_set_size(fullscreen_gauge_container, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_pos(fullscreen_gauge_container, 0, 0);
    lv_obj_set_style_bg_opa(fullscreen_gauge_container, LV_OPA_TRANSP, 0);
    prepare_obj(fullscreen_gauge_container);
    gauge_face = lv_obj_create(fullscreen_gauge_container);
    lv_obj_set_size(gauge_face, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_pos(gauge_face, 0, 0);
    lv_obj_set_style_bg_opa(gauge_face, LV_OPA_TRANSP, 0);
    prepare_obj(gauge_face);

    fullscreen_arc_indicator = lv_arc_create(gauge_face);
    lv_obj_set_size(fullscreen_arc_indicator, GAUGE_ARC_SIZE, GAUGE_ARC_SIZE);
    lv_obj_align(fullscreen_arc_indicator, LV_ALIGN_CENTER, 0, 0);
    prepare_obj(fullscreen_arc_indicator);
    lv_arc_set_bg_angles(fullscreen_arc_indicator, 135, 405);
    lv_arc_set_range(fullscreen_arc_indicator, 0, 1000);
    lv_obj_set_style_arc_color(fullscreen_arc_indicator, color_card_border, LV_PART_MAIN);
    lv_obj_set_style_arc_width(fullscreen_arc_indicator, GAUGE_ARC_STROKE_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(fullscreen_arc_indicator, true, LV_PART_MAIN);
    lv_obj_remove_style(fullscreen_arc_indicator, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(fullscreen_arc_indicator, GAUGE_ARC_STROKE_VALUE, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(fullscreen_arc_indicator, true, LV_PART_INDICATOR);

    info_panel = lv_obj_create(gauge_face);
    lv_obj_set_size(info_panel, GAUGE_INFO_W, GAUGE_INFO_H);
    lv_obj_align(info_panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(info_panel, LV_OPA_TRANSP, 0);
    prepare_obj(info_panel);

    fullscreen_label_name = lv_label_create(info_panel);
    lv_obj_set_width(fullscreen_label_name, GAUGE_INFO_W);
    lv_obj_set_style_text_align(fullscreen_label_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(fullscreen_label_name, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_style_text_font(fullscreen_label_name, UI_FONT_LG, 0);
    lv_obj_set_style_text_color(fullscreen_label_name, color_text, 0);
    lv_obj_set_style_text_letter_space(fullscreen_label_name, 3, 0);

    fullscreen_label_value = lv_label_create(info_panel);
    lv_obj_set_width(fullscreen_label_value, GAUGE_INFO_W);
    lv_obj_set_style_text_align(fullscreen_label_value, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(fullscreen_label_value, "0");
    lv_obj_align(fullscreen_label_value, LV_ALIGN_CENTER, 0, 8);
    lv_obj_set_style_text_font(fullscreen_label_value, GAUGE_VALUE_FONT, 0);
    lv_obj_set_style_text_color(fullscreen_label_value, color_accent, 0);
    lv_obj_remove_flag(fullscreen_label_value, LV_OBJ_FLAG_CLICKABLE);

    fullscreen_label_unit = lv_label_create(info_panel);
    lv_obj_set_width(fullscreen_label_unit, GAUGE_INFO_W);
    lv_obj_set_style_text_align(fullscreen_label_unit, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(fullscreen_label_unit, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_text_font(fullscreen_label_unit, UI_FONT_MD, 0);
    lv_obj_set_style_text_color(fullscreen_label_unit, color_text_dim, 0);
    lv_obj_set_style_text_letter_space(fullscreen_label_unit, 2, 0);

    current_fullscreen_gauge = type;
    last_drawn_value = -1;
    last_drawn_arc = 0xFFFF;
    apply_gauge_content(type);
    last_drawn_value = fullscreen_gauge_values[type];
    last_drawn_arc = calc_arc_value(type);
    last_drawn_arc_color = calc_arc_color(type);
    last_drawn_value_color = calc_value_color(type);
    lv_obj_set_style_opa(gauge_face, LV_OPA_COVER, 0);

    ESP_LOGI(TAG, "Gauge face ready");
}

lv_obj_t *gauge_get_container(void)
{
    return fullscreen_gauge_container;
}

bool gauge_is_transitioning(void)
{
    return gauge_transition_busy;
}

void gauge_cancel_transition(void)
{
    if (gauge_face == NULL) {
        gauge_transition_busy = false;
        return;
    }

    lv_anim_delete(gauge_face, anim_opa_exec);
    gauge_transition_busy = false;
    lv_obj_set_style_opa(gauge_face, LV_OPA_COVER, 0);
}

static void render_gauge_value(gauge_type_t type, int16_t value)
{
    if (type != current_fullscreen_gauge || gauge_transition_busy) {
        return;
    }

    const uint16_t arc_value = calc_arc_value(type);
    if (arc_value != last_drawn_arc) {
        lv_arc_set_value(fullscreen_arc_indicator, arc_value);
        last_drawn_arc = arc_value;
    }

    if (!gauge_value_valid[type]) {
        if (strcmp(lv_label_get_text(fullscreen_label_value), "--") != 0) {
            lv_label_set_text(fullscreen_label_value, "--");
        }
        lv_obj_set_style_text_color(fullscreen_label_value, color_text_dim, 0);
        if (last_drawn_arc != 0) {
            lv_arc_set_value(fullscreen_arc_indicator, 0);
            lv_obj_set_style_arc_color(fullscreen_arc_indicator, color_card_border, LV_PART_INDICATOR);
            last_drawn_arc = 0;
        }
        last_drawn_value = value;
        return;
    }

    if (type == GAUGE_VOLTAGE) {
        snprintf(value_text_buf, sizeof(value_text_buf), "%.1f", value / 10.0f);
    } else if (type == GAUGE_FUEL_CONSUMPTION) {
        snprintf(value_text_buf, sizeof(value_text_buf), "%.1f", value / 10.0f);
    } else {
        snprintf(value_text_buf, sizeof(value_text_buf), "%d", value);
    }
    if (strcmp(lv_label_get_text(fullscreen_label_value), value_text_buf) != 0) {
        lv_label_set_text(fullscreen_label_value, value_text_buf);
    }

    const lv_color_t arc_col = calc_arc_color(type);
    if (lv_color_to_u32(arc_col) != lv_color_to_u32(last_drawn_arc_color)) {
        lv_obj_set_style_arc_color(fullscreen_arc_indicator, arc_col, LV_PART_INDICATOR);
        last_drawn_arc_color = arc_col;
    }

    const lv_color_t val_col = calc_value_color(type);
    if (lv_color_to_u32(val_col) != lv_color_to_u32(last_drawn_value_color)) {
        lv_obj_set_style_text_color(fullscreen_label_value, val_col, 0);
        last_drawn_value_color = val_col;
    }

    last_drawn_value = value;
}

static int16_t smooth_step(gauge_type_t type, int16_t current, int16_t target)
{
    if (current == target) {
        return current;
    }

    int16_t diff = target - current;
    int16_t step = diff / GAUGE_SMOOTH_DIVISOR;

    if (type == GAUGE_RPM) {
        if (step == 0) {
            step = (diff > 0) ? 25 : -25;
        } else if (step > 200) {
            step = 200;
        } else if (step < -200) {
            step = -200;
        }
    } else if (type == GAUGE_SPEED) {
        if (step == 0) {
            step = (diff > 0) ? 1 : -1;
        }
    } else if (step == 0) {
        step = (diff > 0) ? 1 : -1;
    }

    return current + step;
}

void gauge_set_value_valid(gauge_type_t type, bool valid)
{
    if (type < GAUGE_MAX) {
        gauge_value_valid[type] = valid;
    }
}

void gauge_update_fullscreen(gauge_type_t type, int16_t value)
{
    if (type >= GAUGE_MAX) {
        return;
    }

    gauge_target_values[type] = value;
}

void gauge_tick(void)
{
    for (int i = 0; i < GAUGE_MAX; i++) {
        int16_t shown = fullscreen_gauge_values[i];
        int16_t target = gauge_target_values[i];

        if (shown == target) {
            continue;
        }

        fullscreen_gauge_values[i] = smooth_step((gauge_type_t)i, shown, target);
    }

    render_gauge_value(current_fullscreen_gauge,
                       fullscreen_gauge_values[current_fullscreen_gauge]);
}

void gauge_sync_availability(void)
{
    bool any = false;
    for (int i = 0; i < GAUGE_MAX; i++) {
        gauge_nav_available[i] = obd_service_is_gauge_available((uint8_t)i);
        if (gauge_nav_available[i]) {
            any = true;
        }
    }
    if (!any) {
        gauge_nav_available[GAUGE_RPM] = true;
    }
}

bool gauge_is_available(gauge_type_t type)
{
    return type < GAUGE_MAX && gauge_nav_available[type];
}

static gauge_type_t step_available(gauge_type_t from, int direction)
{
    const int start = gauge_order_index(from);
    for (int step = 1; step <= GAUGE_MAX; step++) {
        int pos = start + direction * step;
        while (pos < 0) {
            pos += GAUGE_MAX;
        }
        while (pos >= GAUGE_MAX) {
            pos -= GAUGE_MAX;
        }
        const gauge_type_t candidate = (gauge_type_t)g_settings.gauge_order[pos];
        if (gauge_nav_available[candidate]) {
            return candidate;
        }
    }
    return from;
}

gauge_type_t gauge_next_available(gauge_type_t from)
{
    return step_available(from, 1);
}

gauge_type_t gauge_prev_available(gauge_type_t from)
{
    return step_available(from, -1);
}

gauge_type_t gauge_first_available(void)
{
    for (int i = 0; i < GAUGE_MAX; i++) {
        const gauge_type_t candidate = (gauge_type_t)g_settings.gauge_order[i];
        if (gauge_nav_available[candidate]) {
            return candidate;
        }
    }
    return GAUGE_RPM;
}

void gauge_set_active(gauge_type_t type)
{
    if (type >= GAUGE_MAX || !gauge_nav_available[type] ||
        type == current_fullscreen_gauge) {
        return;
    }

    if (gauge_transition_busy) {
        lv_anim_delete(gauge_face, anim_opa_exec);
        gauge_transition_busy = false;
    }

    last_drawn_value = -1;
    last_drawn_arc = 0xFFFF;
    start_soft_transition(type);
    gauge_update_indicator_row(type);
}

gauge_type_t gauge_get_active(void)
{
    return current_fullscreen_gauge;
}

void gauge_create_indicator_row(lv_obj_t *parent)
{
    const int row_y = UI_GAUGE_DOT_ROW_Y;

    for (int i = 0; i < GAUGE_MAX; i++) {
        indicator_dots[i] = lv_obj_create(parent);
        lv_obj_set_size(indicator_dots[i], UI_GAUGE_DOT_SIZE, UI_GAUGE_DOT_SIZE);
        lv_obj_set_pos(indicator_dots[i], 0, row_y);
        lv_obj_set_style_radius(indicator_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(indicator_dots[i], color_card_border, 0);
        lv_obj_set_style_bg_opa(indicator_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(indicator_dots[i], 0, 0);
        lv_obj_remove_flag(indicator_dots[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(indicator_dots[i], LV_OBJ_FLAG_CLICKABLE);
    }

    indicator_index_label = lv_label_create(parent);
    lv_obj_align(indicator_index_label, LV_ALIGN_TOP_MID, 0, UI_ROUND_INSET / 2);
    lv_obj_set_style_text_font(indicator_index_label, UI_FONT_SM, 0);
    lv_obj_set_style_text_color(indicator_index_label, color_text_dim, 0);
    gauge_update_indicator_row(current_fullscreen_gauge);
}

void gauge_raise_indicator_layers(void)
{
    for (int i = 0; i < GAUGE_MAX; i++) {
        if (indicator_dots[i] != NULL) {
            lv_obj_move_foreground(indicator_dots[i]);
        }
    }
    if (indicator_index_label != NULL) {
        lv_obj_move_foreground(indicator_index_label);
    }
}

void gauge_update_indicator_row(gauge_type_t active)
{
    unsigned visible = 0;
    int active_slot = -1;

    for (int slot = 0; slot < GAUGE_MAX; slot++) {
        const gauge_type_t type = (gauge_type_t)g_settings.gauge_order[slot];
        if (!gauge_nav_available[type]) {
            if (indicator_dots[type] != NULL) {
                lv_obj_add_flag(indicator_dots[type], LV_OBJ_FLAG_HIDDEN);
            }
            continue;
        }
        if (indicator_dots[type] != NULL) {
            lv_obj_remove_flag(indicator_dots[type], LV_OBJ_FLAG_HIDDEN);
        }
        if (type == active) {
            active_slot = (int)visible;
        }
        visible++;
    }

    const int row_w = (int)visible * UI_GAUGE_DOT_SIZE +
                      ((int)visible > 0 ? ((int)visible - 1) * UI_GAUGE_DOT_SPACING : 0);
    const int row_x = (UI_SCREEN_W - row_w) / 2;
    const int row_y = UI_GAUGE_DOT_ROW_Y;
    unsigned draw_slot = 0;

    for (int order_slot = 0; order_slot < GAUGE_MAX; order_slot++) {
        const gauge_type_t type = (gauge_type_t)g_settings.gauge_order[order_slot];
        if (!gauge_nav_available[type] || indicator_dots[type] == NULL) {
            continue;
        }
        lv_obj_set_pos(indicator_dots[type],
                       row_x + (int)draw_slot * (UI_GAUGE_DOT_SIZE + UI_GAUGE_DOT_SPACING), row_y);
        lv_obj_set_style_bg_color(indicator_dots[type],
                                  (type == active) ? lv_color_hex(gauge_get_color(type))
                                                   : color_card_border,
                                  0);
        draw_slot++;
    }

    char buf[24];
    if (visible == 0) {
        snprintf(buf, sizeof(buf), "--");
    } else {
        snprintf(buf, sizeof(buf), "%d/%u",
                 active_slot >= 0 ? active_slot + 1 : 1, visible);
    }
    if (indicator_index_label != NULL) {
        lv_label_set_text(indicator_index_label, buf);
        lv_obj_set_style_text_color(indicator_index_label,
                                    lv_color_hex(gauge_get_color(active)), 0);
    }
}
