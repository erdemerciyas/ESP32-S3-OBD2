#include "screen_splash.h"
#include "theme.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#define SPLASH_DURATION_MS 3000

static lv_obj_t *s_spinner_arc;
static lv_timer_t *s_splash_timer;
static lv_timer_t *s_anim_timer;
static lv_timer_cb_t s_finish_cb;

static void splash_anim_cb(lv_timer_t *timer)
{
    (void)timer;
    uint32_t now = lv_tick_get();
    float phase = (now % 1000) / 1000.0f;
    float v = (sinf(phase * 2.0f * M_PI - M_PI / 2.0f) + 1.0f) * 0.5f;
    int val = (int)(v * 100.0f);
    lv_arc_set_value(s_spinner_arc, val);
}

static void splash_timer_cb(lv_timer_t *timer)
{
    lv_timer_del(timer);
    s_splash_timer = NULL;

    if (s_anim_timer) {
        lv_timer_del(s_anim_timer);
        s_anim_timer = NULL;
    }

    if (s_finish_cb) {
        s_finish_cb(timer);
    }
}

lv_obj_t *screen_splash_create(lv_obj_t *parent)
{
    const ui_theme_t *t = theme_get();

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, UI_VIEWPORT_SZ, UI_VIEWPORT_SZ);
    lv_obj_align(root, LV_ALIGN_CENTER, 0, 0);
    theme_apply_screen(root);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(root, 24, 0);

    s_spinner_arc = lv_arc_create(root);
    lv_obj_set_size(s_spinner_arc, 80, 80);
    lv_arc_set_rotation(s_spinner_arc, 135);
    lv_arc_set_bg_angles(s_spinner_arc, 0, 270);
    lv_arc_set_value(s_spinner_arc, 0);
    lv_arc_set_range(s_spinner_arc, 0, 100);
    lv_obj_remove_style(s_spinner_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_spinner_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(s_spinner_arc, t->primary, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_spinner_arc, t->arc_bg, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_spinner_arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_spinner_arc, 8, LV_PART_INDICATOR);

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "OBD2 DASH");
    lv_obj_set_style_text_font(title, t->font_xxl, 0);
    lv_obj_set_style_text_color(title, t->primary, 0);

    lv_obj_t *sub = lv_label_create(root);
    lv_label_set_text(sub, "Loading...");
    lv_obj_set_style_text_font(sub, t->font_md, 0);
    lv_obj_set_style_text_color(sub, t->text_dim, 0);

    return root;
}

void screen_splash_start(lv_obj_t *splash, lv_timer_cb_t on_finish)
{
    (void)splash;
    s_finish_cb = on_finish;
    s_splash_timer = lv_timer_create(splash_timer_cb, SPLASH_DURATION_MS, NULL);
    s_anim_timer = lv_timer_create(splash_anim_cb, 30, NULL);
}
