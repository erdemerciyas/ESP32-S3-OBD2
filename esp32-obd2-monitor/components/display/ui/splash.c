#include "splash.h"
#include "styles.h"
#include "board_config.h"
#include "app.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "splash";

#define SPLASH_ARC_MARGIN        28
#define SPLASH_ARC_SIZE          (UI_SCREEN_W - (SPLASH_ARC_MARGIN * 2))
#define SPLASH_ARC_TRACK         12
#define SPLASH_ARC_VALUE         14
#define SPLASH_LOAD_FRAMES       50
#define SPLASH_FRAME_MS          16
#define SPLASH_HOLD_FRAMES       8

static void splash_prepare_screen(lv_obj_t *scr)
{
    lv_obj_set_size(scr, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_radius(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(scr, style_get_bg_dark(), 0);
    lv_obj_set_style_bg_color(scr, color_bg_dark, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
}

static void splash_pump(int frames)
{
    for (int i = 0; i < frames; i++) {
        lv_timer_handler();
        lv_refr_now(NULL);
        vTaskDelay(pdMS_TO_TICKS(SPLASH_FRAME_MS));
    }
}

void splash_run_boot_animation(lv_obj_t *next_screen)
{
    ESP_LOGI(TAG, "Boot splash start");

    lv_obj_t *scr = lv_obj_create(NULL);
    splash_prepare_screen(scr);
    lv_scr_load(scr);
    lv_refr_now(NULL);

    lv_obj_t *ring = lv_arc_create(scr);
    lv_obj_set_size(ring, SPLASH_ARC_SIZE, SPLASH_ARC_SIZE);
    lv_obj_align(ring, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_bg_angles(ring, 135, 405);
    lv_arc_set_range(ring, 0, 1000);
    lv_arc_set_value(ring, 0);
    lv_obj_remove_style(ring, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(ring, color_card_border, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring, SPLASH_ARC_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(ring, true, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring, color_primary, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(ring, SPLASH_ARC_VALUE, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(ring, true, LV_PART_INDICATOR);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "EXTREME");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -8);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(title, color_accent, 0);

    lv_obj_t *subtitle = lv_label_create(scr);
    lv_label_set_text(subtitle, "MONITOR");
    lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 38);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(subtitle, color_text_dim, 0);
    lv_obj_set_style_text_letter_space(subtitle, 6, 0);

    lv_obj_t *status = lv_label_create(scr);
    lv_label_set_text(status, "BASLATILIYOR");
    lv_obj_align(status, LV_ALIGN_BOTTOM_MID, 0, -72);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status, color_primary, 0);
    lv_obj_set_style_text_letter_space(status, 3, 0);

    lv_obj_t *ver = lv_label_create(scr);
    lv_label_set_text_fmt(ver, "v%s", APP_VERSION);
    lv_obj_align(ver, LV_ALIGN_BOTTOM_MID, 0, -44);
    lv_obj_set_style_text_font(ver, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(ver, color_text_dim, 0);

    for (int frame = 0; frame <= SPLASH_LOAD_FRAMES; frame++) {
        const int32_t pct = (frame * 1000) / SPLASH_LOAD_FRAMES;
        lv_arc_set_value(ring, pct);
        lv_timer_handler();
        lv_refr_now(NULL);
        vTaskDelay(pdMS_TO_TICKS(SPLASH_FRAME_MS));
    }

    splash_pump(SPLASH_HOLD_FRAMES);

    if (next_screen != NULL) {
        lv_scr_load(next_screen);
        lv_refr_now(NULL);
    }
    lv_obj_delete(scr);
    lv_refr_now(NULL);

    ESP_LOGI(TAG, "Boot splash done");
}
