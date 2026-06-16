#include "theme.h"
#include "vehicle_data.h"

extern const lv_font_t lv_font_montserrat_56;
extern const lv_font_t lv_font_montserrat_94_bold;

static ui_theme_t s_theme = {
    .bg         = LV_COLOR_MAKE(0x04, 0x08, 0x0C),
    .surface    = LV_COLOR_MAKE(0x0C, 0x14, 0x1E),
    .surface_hi = LV_COLOR_MAKE(0x14, 0x1E, 0x2C),
    .primary    = LV_COLOR_MAKE(0x00, 0xD4, 0xAA),
    .secondary  = LV_COLOR_MAKE(0x38, 0xBD, 0xF8),
    .text       = LV_COLOR_MAKE(0xF1, 0xF5, 0xF9),
    .text_dim   = LV_COLOR_MAKE(0x8B, 0x9A, 0xB0),
    .ok         = LV_COLOR_MAKE(0x22, 0xC5, 0x5E),
    .warn       = LV_COLOR_MAKE(0xFA, 0xCC, 0x15),
    .crit       = LV_COLOR_MAKE(0xEF, 0x44, 0x44),
    .arc_bg     = LV_COLOR_MAKE(0x16, 0x20, 0x30),
    .border     = LV_COLOR_MAKE(0x20, 0x30, 0x44),
    .font_sm    = &lv_font_montserrat_16,
    .font_md    = &lv_font_montserrat_20,
    .font_lg    = &lv_font_montserrat_28,
    .font_xl    = &lv_font_montserrat_48,
    .font_xxl   = &lv_font_montserrat_56,
    .font_value = &lv_font_montserrat_94_bold,
    .font_data  = &lv_font_montserrat_28,
};

const ui_theme_t *theme_get(void)
{
    return &s_theme;
}

lv_color_t theme_threshold_color(threshold_level_t level)
{
    switch (level) {
    case THRESHOLD_WARN: return s_theme.warn;
    case THRESHOLD_CRIT: return s_theme.crit;
    default:             return s_theme.ok;
    }
}

static uint8_t lerp_u8(uint8_t a, uint8_t b, float t)
{
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    return (uint8_t)(a + (b - a) * t);
}

static lv_color_t lerp_color(lv_color_t c1, lv_color_t c2, float t)
{
    lv_color_t r;
    r.ch.red   = lerp_u8(c1.ch.red,   c2.ch.red,   t);
    r.ch.green = lerp_u8(c1.ch.green, c2.ch.green, t);
    r.ch.blue  = lerp_u8(c1.ch.blue,  c2.ch.blue,  t);
    return r;
}

lv_color_t theme_rpm_gradient_color(float rpm)
{
    /* Smooth gradient: green -> yellow -> orange -> red */
    static const lv_color_t c_green  = LV_COLOR_MAKE(0x22, 0xC5, 0x5E);
    static const lv_color_t c_yellow = LV_COLOR_MAKE(0xFA, 0xCC, 0x15);
    static const lv_color_t c_orange = LV_COLOR_MAKE(0xFF, 0x98, 0x00);
    static const lv_color_t c_red    = LV_COLOR_MAKE(0xEF, 0x44, 0x44);

    if (rpm <= 3000.0f) return c_green;
    if (rpm <= 4000.0f) return lerp_color(c_green,  c_yellow, (rpm - 3000.0f) / 1000.0f);
    if (rpm <= 6000.0f) return lerp_color(c_yellow, c_orange, (rpm - 4000.0f) / 2000.0f);
    if (rpm <= 7000.0f) return lerp_color(c_orange, c_red,    (rpm - 6000.0f) / 1000.0f);
    return c_red;
}

lv_color_t theme_speed_gradient_color(float kmh)
{
    /* Smooth gradient: green (0-80) -> yellow (80-100) -> orange (100-120) -> red (120+) */
    static const lv_color_t c_green  = LV_COLOR_MAKE(0x22, 0xC5, 0x5E);
    static const lv_color_t c_yellow = LV_COLOR_MAKE(0xFA, 0xCC, 0x15);
    static const lv_color_t c_orange = LV_COLOR_MAKE(0xFF, 0x98, 0x00);
    static const lv_color_t c_red    = LV_COLOR_MAKE(0xEF, 0x44, 0x44);

    if (kmh <= 80.0f)  return c_green;
    if (kmh <= 100.0f) return lerp_color(c_green,  c_yellow, (kmh - 80.0f)  / 20.0f);
    if (kmh <= 120.0f) return lerp_color(c_yellow, c_orange, (kmh - 100.0f) / 20.0f);
    if (kmh <= 160.0f) return lerp_color(c_orange, c_red,    (kmh - 120.0f) / 40.0f);
    return c_red;
}

void theme_apply_bg(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, s_theme.bg, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
}

void theme_apply_screen(lv_obj_t *obj)
{
    theme_apply_bg(obj);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void theme_apply_content(lv_obj_t *obj)
{
    theme_apply_screen(obj);
    lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(obj, UI_PAD_HOR, 0);
    lv_obj_set_style_pad_top(obj, UI_PAD_TOP, 0);
    lv_obj_set_style_pad_bottom(obj, UI_PAD_BOT, 0);
    lv_obj_set_style_pad_row(obj, UI_GAP, 0);
}

void theme_apply_card(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, s_theme.surface, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, s_theme.border, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_50, 0);
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *theme_create_flex_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, UI_GAP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

lv_obj_t *theme_create_flex_col(lv_obj_t *parent, bool grow)
{
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_set_width(col, LV_PCT(100));
    if (grow) {
        lv_obj_set_flex_grow(col, 1);
    }
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_style_pad_row(col, UI_GAP, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    return col;
}

lv_obj_t *theme_create_header(lv_obj_t *parent, const char *title)
{
    const ui_theme_t *t = theme_get();

    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, title);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl, t->font_lg, 0);
    lv_obj_set_style_text_color(lbl, t->text, 0);
    return lbl;
}

lv_obj_t *theme_create_metric_cell(lv_obj_t *parent, const char *label, lv_obj_t **value_out)
{
    const ui_theme_t *t = theme_get();

    lv_obj_t *cell = lv_obj_create(parent);
    theme_apply_card(cell);
    lv_obj_set_size(cell, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(cell, 1);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(cell, 6, 0);
    lv_obj_set_style_pad_ver(cell, 8, 0);
    lv_obj_set_style_pad_row(cell, 5, 0);

    lv_obj_t *name = lv_label_create(cell);
    lv_label_set_text(name, label);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, LV_PCT(100));
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(name, t->font_sm, 0);
    lv_obj_set_style_text_color(name, t->text_dim, 0);

    lv_obj_t *val = lv_label_create(cell);
    lv_label_set_text(val, "--");
    lv_label_set_long_mode(val, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(val, LV_PCT(100));
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(val, t->font_data, 0);
    lv_obj_set_style_text_color(val, t->text, 0);

    if (value_out) {
        *value_out = val;
    }

    return cell;
}
