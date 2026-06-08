#include "splash_ui.h"
#include "ui_theme.h"
#include "ui_fonts.h"
#include "config.h"

using namespace UiTheme;

void SplashUi::create() {
    screen_ = lv_obj_create(NULL);
    applyScreen(screen_);

    lv_obj_t *title = lv_label_create(screen_);
    lv_label_set_text(title, LV_SYMBOL_DRIVE " OBD");
    lv_obj_set_style_text_font(title, UI_FONT_MD, 0);
    lv_obj_set_style_text_color(title, color(kInk100), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -48);

    /* Statik yay — lv_spinner surekli animasyon RGB panelde titreme yapar */
    waitArc_ = lv_arc_create(screen_);
    lv_obj_set_size(waitArc_, 64, 64);
    lv_obj_align(waitArc_, LV_ALIGN_CENTER, 0, 8);
    lv_arc_set_rotation(waitArc_, 135);
    lv_arc_set_bg_angles(waitArc_, 0, 270);
    lv_arc_set_angles(waitArc_, 0, 72);
    lv_obj_remove_style(waitArc_, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(waitArc_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(waitArc_, color(kInk500), LV_PART_MAIN);
    lv_obj_set_style_arc_color(waitArc_, color(kAmber300), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(waitArc_, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(waitArc_, 6, LV_PART_INDICATOR);

    lv_obj_t *sub = lv_label_create(screen_);
    lv_label_set_text(sub, "Yukleniyor...");
    lv_obj_set_style_text_font(sub, UI_FONT_SM, 0);
    lv_obj_set_style_text_color(sub, color(kInk300), 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 72);
}

void SplashUi::show() {
    visible_ = true;
    lv_scr_load(screen_);
}

void SplashUi::hide() {
    visible_ = false;
}
