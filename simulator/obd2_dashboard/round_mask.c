#include "round_mask.h"
#include "theme.h"
#include "lvgl.h"
#include <math.h>

#define MASK_MARKER_SZ  6

static lv_obj_t *s_guide_outer;
static lv_obj_t *s_guide_visible;

static lv_obj_t *create_circle_guide(lv_coord_t diameter, lv_color_t color, lv_coord_t width)
{
    lv_obj_t *arc = lv_arc_create(lv_layer_top());
    lv_obj_set_size(arc, diameter, diameter);
    lv_obj_center(arc);
    lv_arc_set_rotation(arc, 0);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_range(arc, 0, 360);
    lv_arc_set_value(arc, 360);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(arc, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_arc_color(arc, color, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    return arc;
}

static void place_marker(lv_coord_t x, lv_coord_t y, bool inside)
{
    lv_obj_t *m = lv_obj_create(lv_layer_top());
    lv_obj_set_size(m, MASK_MARKER_SZ, MASK_MARKER_SZ);
    lv_obj_set_pos(m, x - MASK_MARKER_SZ / 2, y - MASK_MARKER_SZ / 2);
    lv_obj_set_style_radius(m, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(m, 0, 0);
    lv_obj_set_style_bg_opa(m, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(m, inside ? lv_color_hex(0x22C55E) : lv_color_hex(0xEF4444), 0);
    lv_obj_clear_flag(m, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(m, LV_OBJ_FLAG_IGNORE_LAYOUT);
}

static bool point_inside_visible_circle(lv_coord_t x, lv_coord_t y)
{
    const float cx = UI_PANEL_D / 2.0f;
    const float cy = UI_PANEL_D / 2.0f;
    const float r = UI_VIEWPORT_SZ / 2.0f;
    const float dx = (float)x - cx;
    const float dy = (float)y - cy;
    return (dx * dx + dy * dy) <= (r * r);
}

void round_mask_init(void)
{
    const lv_coord_t tab_x = UI_BEZEL;
    const lv_coord_t tab_y = UI_BEZEL;
    const lv_coord_t tab_w = UI_VIEWPORT_SZ;
    const lv_coord_t tab_h = UI_VIEWPORT_SZ;
    const lv_coord_t tab_b = tab_y + tab_h;

    const lv_coord_t gauge_cx = tab_x + tab_w / 2;
    const lv_coord_t gauge_cy = tab_y + tab_h / 2 - 20;
    const lv_coord_t gauge_half = UI_GAUGE_SZ / 2;

    const lv_coord_t stat_lift = UI_STAT_LIFT + UI_DOT_H;
    const lv_coord_t stat_b = tab_b - stat_lift;
    const lv_coord_t stat_t = stat_b - UI_STAT_H;

    const lv_coord_t btn_b = stat_b;
    const lv_coord_t btn_t = btn_b - UI_BTN_H;

    const lv_coord_t stat_mid_y = (lv_coord_t)(stat_t + UI_STAT_H / 2);

    const lv_coord_t mx[] = {
        (lv_coord_t)(gauge_cx - gauge_half),
        (lv_coord_t)(gauge_cx + gauge_half),
        (lv_coord_t)(gauge_cx - gauge_half),
        (lv_coord_t)(gauge_cx + gauge_half),
        tab_x,
        (lv_coord_t)(tab_x + tab_w),
        tab_x,
        (lv_coord_t)(tab_x + tab_w),
        tab_x,
        (lv_coord_t)(tab_x + tab_w),
        (lv_coord_t)(tab_x + tab_w / 2),
    };
    const lv_coord_t my[] = {
        (lv_coord_t)(gauge_cy - gauge_half),
        (lv_coord_t)(gauge_cy - gauge_half),
        (lv_coord_t)(gauge_cy + gauge_half),
        (lv_coord_t)(gauge_cy + gauge_half),
        stat_t,
        stat_t,
        stat_b,
        stat_b,
        btn_t,
        btn_b,
        stat_mid_y,
    };
    const size_t marker_count = sizeof(mx) / sizeof(mx[0]);

    s_guide_outer = create_circle_guide(UI_PANEL_D, lv_color_hex(0x7A8AA2), 1);
    s_guide_visible = create_circle_guide(UI_VIEWPORT_SZ, lv_color_hex(0x00D4AA), 2);

    (void)s_guide_outer;
    (void)s_guide_visible;

    for (size_t i = 0; i < marker_count; i++) {
        place_marker(mx[i], my[i], point_inside_visible_circle(mx[i], my[i]));
    }
}
