#include "theme.h"
#include "vehicle_data.h"
#include <math.h>

extern const lv_font_t lv_font_montserrat_56;
extern const lv_font_t lv_font_montserrat_94_bold;

static ui_theme_t s_theme = {
    .bg         = LV_COLOR_MAKE(0x02, 0x06, 0x0A),
    .surface    = LV_COLOR_MAKE(0x0A, 0x10, 0x18),
    .surface_hi = LV_COLOR_MAKE(0x12, 0x1C, 0x28),
    .primary    = LV_COLOR_MAKE(0x00, 0xF0, 0xFF),  /* Cyan */
    .secondary  = LV_COLOR_MAKE(0xFF, 0x91, 0x00),  /* Orange */
    .accent     = LV_COLOR_MAKE(0xFF, 0x2A, 0x2A),  /* Racing red */
    .text       = LV_COLOR_MAKE(0xF0, 0xF0, 0xF0),
    .text_dim   = LV_COLOR_MAKE(0x6B, 0x7A, 0x8F),
    .ok         = LV_COLOR_MAKE(0x00, 0xE6, 0x76),
    .warn       = LV_COLOR_MAKE(0xFF, 0xC4, 0x00),
    .crit       = LV_COLOR_MAKE(0xFF, 0x2A, 0x2A),
    .arc_bg     = LV_COLOR_MAKE(0x08, 0x12, 0x1C),
    .border     = LV_COLOR_MAKE(0x1C, 0x2A, 0x3C),
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

lv_coord_t ui_chord_width_at_y(lv_coord_t y_tab)
{
    const lv_coord_t cy = UI_CIRCLE_CY;
    const lv_coord_t r = (UI_VIEWPORT_SZ / 2) - UI_SAFE_MARGIN;
    lv_coord_t dy = y_tab - cy;

    if (dy < 0) {
        dy = -dy;
    }
    if (dy >= r) {
        return 0;
    }

    float hw = sqrtf((float)(r * r - dy * dy));
    return (lv_coord_t)(hw * 2.0f);
}

lv_coord_t theme_safe_width(lv_coord_t y_top, lv_coord_t y_bottom)
{
    lv_coord_t w_top = ui_chord_width_at_y(y_top);
    lv_coord_t w_bot = ui_chord_width_at_y(y_bottom);
    lv_coord_t w = w_top;

    if (w_bot < w) {
        w = w_bot;
    }
    return w;
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
    /* Racing gradient: cyan -> orange -> red */
    static const lv_color_t c_cyan   = LV_COLOR_MAKE(0x00, 0xF0, 0xFF);
    static const lv_color_t c_orange = LV_COLOR_MAKE(0xFF, 0x91, 0x00);
    static const lv_color_t c_red    = LV_COLOR_MAKE(0xFF, 0x2A, 0x2A);

    if (rpm <= 3000.0f) return c_cyan;
    if (rpm <= 5000.0f) return lerp_color(c_cyan,   c_orange, (rpm - 3000.0f) / 2000.0f);
    if (rpm <= 6500.0f) return lerp_color(c_orange, c_red,    (rpm - 5000.0f) / 1500.0f);
    return c_red;
}

lv_color_t theme_speed_gradient_color(float kmh)
{
    /* Racing gradient: cyan -> orange -> red */
    static const lv_color_t c_cyan   = LV_COLOR_MAKE(0x00, 0xF0, 0xFF);
    static const lv_color_t c_orange = LV_COLOR_MAKE(0xFF, 0x91, 0x00);
    static const lv_color_t c_red    = LV_COLOR_MAKE(0xFF, 0x2A, 0x2A);

    if (kmh <= 80.0f)  return c_cyan;
    if (kmh <= 120.0f) return lerp_color(c_cyan,   c_orange, (kmh - 80.0f)  / 40.0f);
    if (kmh <= 180.0f) return lerp_color(c_orange, c_red,    (kmh - 120.0f) / 60.0f);
    return c_red;
}

lv_color_t theme_shift_light_color(float rpm_ratio)
{
    static const lv_color_t c_dim    = LV_COLOR_MAKE(0x20, 0x30, 0x40);
    static const lv_color_t c_cyan   = LV_COLOR_MAKE(0x00, 0xF0, 0xFF);
    static const lv_color_t c_orange = LV_COLOR_MAKE(0xFF, 0x91, 0x00);
    static const lv_color_t c_red    = LV_COLOR_MAKE(0xFF, 0x2A, 0x2A);

    if (rpm_ratio < 0.70f) return c_dim;
    if (rpm_ratio < 0.80f) return c_cyan;
    if (rpm_ratio < 0.90f) return lerp_color(c_cyan, c_orange, (rpm_ratio - 0.80f) / 0.10f);
    if (rpm_ratio < 0.95f) return c_orange;
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

/* Glass morphism data pill: semi-transparent surface, subtle gradient border,
 * soft shadow for depth, larger radius. Used for the dashboard data strip. */
void theme_apply_glass_card(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, s_theme.surface, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_80, 0);
    lv_obj_set_style_border_color(obj, s_theme.border, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_60, 0);
    lv_obj_set_style_radius(obj, UI_DATA_RADIUS, 0);
    lv_obj_set_style_shadow_width(obj, 8, 0);
    lv_obj_set_style_shadow_ofs_y(obj, 2, 0);
    lv_obj_set_style_shadow_color(obj, s_theme.bg, 0);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_30, 0);
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

lv_obj_t *theme_create_metric_cell(lv_obj_t *parent, const char *label,
                                    lv_color_t accent, lv_obj_t **value_out,
                                    lv_obj_t **unit_out)
{
    const ui_theme_t *t = theme_get();

    lv_obj_t *cell = lv_obj_create(parent);
    theme_apply_card(cell);
    lv_obj_set_style_border_color(cell, accent, 0);
    lv_obj_set_style_border_width(cell, 3, 0);
    lv_obj_set_style_border_opa(cell, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(cell, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_size(cell, LV_PCT(33), LV_PCT(100));
    lv_obj_set_flex_grow(cell, 1);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(cell, UI_GAP_SM, 0);
    lv_obj_set_style_pad_ver(cell, UI_GAP_MD, 0);
    lv_obj_set_style_pad_row(cell, UI_GAP_SM, 0);

    lv_obj_t *name = lv_label_create(cell);
    lv_label_set_text(name, label);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, LV_PCT(100));
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(name, t->font_sm,  0);
    lv_obj_set_style_text_color(name, t->text_dim, 0);

    lv_obj_t *val = lv_label_create(cell);
    lv_label_set_text(val, "--");
    lv_label_set_long_mode(val, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(val, LV_PCT(100));
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(val, t->font_data, 0);
    lv_obj_set_style_text_color(val, t->text, 0);

    lv_obj_t *unit = lv_label_create(cell);
    lv_label_set_text(unit, "");
    lv_label_set_long_mode(unit, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(unit, LV_PCT(100));
    lv_obj_set_style_text_align(unit, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(unit, t->font_sm, 0);
    lv_obj_set_style_text_color(unit, t->text_dim, 0);

    if (value_out) {
        *value_out = val;
    }
    if (unit_out) {
        *unit_out = unit;
    }

    return cell;
}

/* Compact stat cell for dashboard bottom row — top accent + value/unit pair */
lv_obj_t *theme_create_stat_cell(lv_obj_t *parent, const char *label,
                                  lv_obj_t **value_out, lv_obj_t **unit_out)
{
    const ui_theme_t *t = theme_get();

    lv_obj_t *cell = lv_obj_create(parent);
    theme_apply_card(cell);
    theme_apply_card_topline(cell, t->border);
    lv_obj_set_size(cell, LV_PCT(33), LV_PCT(100));
    lv_obj_set_flex_grow(cell, 1);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(cell, 3, 0);
    lv_obj_set_style_pad_ver(cell, 5, 0);
    lv_obj_set_style_pad_row(cell, 2, 0);

    lv_obj_t *name = lv_label_create(cell);
    lv_label_set_text(name, label);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, LV_PCT(100));
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(name, t->font_sm, 0);
    lv_obj_set_style_text_color(name, t->text_dim, 0);

    lv_obj_t *val_row = lv_obj_create(cell);
    lv_obj_set_width(val_row, LV_PCT(100));
    lv_obj_set_style_bg_opa(val_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(val_row, 0, 0);
    lv_obj_set_style_pad_all(val_row, 0, 0);
    lv_obj_set_flex_flow(val_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(val_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(val_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *val = lv_label_create(val_row);
    lv_label_set_text(val, "--");
    lv_label_set_long_mode(val, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(val, t->font_data, 0);
    lv_obj_set_style_text_color(val, t->text, 0);

    lv_obj_t *unit = lv_label_create(val_row);
    lv_label_set_text(unit, "");
    lv_obj_set_style_text_align(unit, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(unit, t->font_sm, 0);
    lv_obj_set_style_text_color(unit, t->text_dim, 0);
    lv_obj_set_style_pad_left(unit, 3, 0);

    if (value_out) {
        *value_out = val;
    }
    if (unit_out) {
        *unit_out = unit;
    }

    return cell;
}

void theme_apply_card_topline(lv_obj_t *obj, lv_color_t color)
{
    lv_obj_set_style_border_color(obj, color, 0);
    lv_obj_set_style_border_width(obj, 2, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_TOP, 0);
}

void theme_apply_setting_row(lv_obj_t *obj, lv_color_t accent)
{
    theme_apply_card(obj);
    lv_obj_set_style_border_color(obj, accent, 0);
    lv_obj_set_style_border_width(obj, 3, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_pad_left(obj, 10, 0);
    lv_obj_set_style_pad_right(obj, 8, 0);
}
