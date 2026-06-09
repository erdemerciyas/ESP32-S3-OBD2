#include "styles.h"
#include "ui_fonts.h"
#include "app.h"
#include "esp_log.h"

static const char *TAG = "styles";

/* Workshop-at-dusk palette (initialized in styles_init) */
lv_color_t color_primary;
lv_color_t color_secondary;
lv_color_t color_accent;
lv_color_t color_success;
lv_color_t color_warning;
lv_color_t color_danger;
lv_color_t color_text;
lv_color_t color_text_dim;
lv_color_t color_bg_dark;
lv_color_t color_card_bg;
lv_color_t color_card_border;

/* Style objects */
static lv_style_t style_bg_dark;
static lv_style_t style_card;
static lv_style_t style_card_pressed;
static lv_style_t style_btn_primary;
static lv_style_t style_btn_secondary;
static lv_style_t style_text_title;
static lv_style_t style_text_normal;
static lv_style_t style_text_dim;
static lv_style_t style_text_accent;
static lv_style_t style_label;
static lv_style_t style_toggle;
static lv_style_t style_slider;

/* Carbon fiber pattern - 4x4 grid */
static lv_style_t style_carbon_bg;

void styles_init(uint8_t theme_mode)
{
    ESP_LOGI(TAG, "Initializing theme mode %u", theme_mode);

    if (theme_mode == THEME_LIGHT) {
        color_primary = APP_COLOR_RGB(0xC6, 0x6A, 0x0C);
        color_secondary = APP_COLOR_RGB(0x8B, 0x3A, 0x1A);
        color_accent = APP_COLOR_RGB(0xE6, 0x8A, 0x10);
        color_success = APP_COLOR_RGB(0x2A, 0x9A, 0x88);
        color_warning = APP_COLOR_RGB(0xD6, 0x8A, 0x20);
        color_danger = APP_COLOR_RGB(0xA0, 0x3A, 0x20);
        color_text = APP_COLOR_RGB(0x18, 0x16, 0x0F);
        color_text_dim = APP_COLOR_RGB(0x6B, 0x62, 0x53);
        color_bg_dark = APP_COLOR_RGB(0xF4, 0xEB, 0xD9);
        color_card_bg = APP_COLOR_RGB(0xFF, 0xFF, 0xFA);
        color_card_border = APP_COLOR_RGB(0xD9, 0xCD, 0xB4);
    } else {
        color_primary = APP_COLOR_RGB(0xF0, 0x8A, 0x1C);
        color_secondary = APP_COLOR_RGB(0xB1, 0x4A, 0x2A);
        color_accent = APP_COLOR_RGB(0xFF, 0xB4, 0x4A);
        color_success = APP_COLOR_RGB(0x4A, 0xD6, 0xC2);
        color_warning = APP_COLOR_RGB(0xFF, 0xB4, 0x4A);
        color_danger = APP_COLOR_RGB(0xB1, 0x4A, 0x2A);
        color_text = APP_COLOR_RGB(0xF4, 0xEB, 0xD9);
        color_text_dim = APP_COLOR_RGB(0x6B, 0x62, 0x53);
        color_bg_dark = APP_COLOR_RGB(0x05, 0x06, 0x05);
        color_card_bg = APP_COLOR_RGB(0x18, 0x16, 0x0F);
        color_card_border = APP_COLOR_RGB(0x3B, 0x36, 0x2D);
    }

    /* Background styles */
    lv_style_init(&style_bg_dark);
    lv_style_set_bg_color(&style_bg_dark, color_bg_dark);
    lv_style_set_radius(&style_bg_dark, 0);

    /* Carbon fiber background pattern */
    lv_style_init(&style_carbon_bg);
    lv_style_set_bg_color(&style_carbon_bg, color_bg_dark);
    lv_style_set_bg_grad_color(&style_carbon_bg, APP_COLOR_RGB(0x0F, 0x0F, 0x0F));
    lv_style_set_bg_grad_dir(&style_carbon_bg, LV_GRAD_DIR_NONE);

    /* Card style */
    lv_style_init(&style_card);
    lv_style_set_bg_color(&style_card, color_card_bg);
    lv_style_set_border_color(&style_card, color_card_border);
    lv_style_set_border_width(&style_card, 1);
    lv_style_set_radius(&style_card, 15);
    lv_style_set_pad_all(&style_card, 15);
    lv_style_set_shadow_width(&style_card, 8);
    lv_style_set_shadow_color(&style_card, APP_COLOR_RGB(0x00, 0x00, 0x00));

    /* Card pressed style */
    lv_style_init(&style_card_pressed);
    lv_style_set_bg_color(&style_card_pressed, APP_COLOR_RGB(0x22, 0x22, 0x22));
    lv_style_set_border_color(&style_card_pressed, color_primary);
    lv_style_set_border_width(&style_card_pressed, 1);
    lv_style_set_radius(&style_card_pressed, 15);

    /* Primary button style (red) */
    lv_style_init(&style_btn_primary);
    lv_style_set_bg_color(&style_btn_primary, color_primary);
    lv_style_set_text_color(&style_btn_primary, color_text);
    lv_style_set_radius(&style_btn_primary, 25);
    lv_style_set_pad_hor(&style_btn_primary, 20);
    lv_style_set_pad_ver(&style_btn_primary, 12);

    /* Secondary button style (outlined) */
    lv_style_init(&style_btn_secondary);
    lv_style_set_bg_color(&style_btn_secondary, APP_COLOR_RGB(0x00, 0x00, 0x00));
    lv_style_set_border_color(&style_btn_secondary, color_primary);
    lv_style_set_border_width(&style_btn_secondary, 1);
    lv_style_set_text_color(&style_btn_secondary, color_primary);
    lv_style_set_radius(&style_btn_secondary, 25);
    lv_style_set_pad_hor(&style_btn_secondary, 20);
    lv_style_set_pad_ver(&style_btn_secondary, 12);

    /* Title text style */
    lv_style_init(&style_text_title);
    lv_style_set_text_color(&style_text_title, color_primary);
    lv_style_set_text_font(&style_text_title, UI_FONT_XL);
    lv_style_set_text_align(&style_text_title, LV_TEXT_ALIGN_CENTER);

    /* Normal text style */
    lv_style_init(&style_text_normal);
    lv_style_set_text_color(&style_text_normal, color_text);
    lv_style_set_text_font(&style_text_normal, UI_FONT_LG);

    /* Dim text style */
    lv_style_init(&style_text_dim);
    lv_style_set_text_color(&style_text_dim, color_text_dim);
    lv_style_set_text_font(&style_text_dim, UI_FONT_SM);

    /* Accent text style */
    lv_style_init(&style_text_accent);
    lv_style_set_text_color(&style_text_accent, color_accent);
    lv_style_set_text_font(&style_text_accent, UI_FONT_MD);

    /* Label style */
    lv_style_init(&style_label);
    lv_style_set_text_color(&style_label, color_text);
    lv_style_set_text_font(&style_label, UI_FONT_SM);
    lv_style_set_text_color(&style_label, color_text_dim);

    /* Toggle style */
    lv_style_init(&style_toggle);
    lv_style_set_bg_color(&style_toggle, color_card_border);
    lv_style_set_radius(&style_toggle, 12);

    /* Slider style */
    lv_style_init(&style_slider);
    lv_style_set_bg_color(&style_slider, color_card_border);
    lv_style_set_radius(&style_slider, 2);

    ESP_LOGI(TAG, "Theme styles initialized");
}

lv_style_t *style_get_bg_dark(void) { return &style_bg_dark; }
lv_style_t *style_get_card(void) { return &style_card; }
lv_style_t *style_get_card_pressed(void) { return &style_card_pressed; }
lv_style_t *style_get_btn_primary(void) { return &style_btn_primary; }
lv_style_t *style_get_btn_secondary(void) { return &style_btn_secondary; }
lv_style_t *style_get_text_title(void) { return &style_text_title; }
lv_style_t *style_get_text_normal(void) { return &style_text_normal; }
lv_style_t *style_get_text_dim(void) { return &style_text_dim; }
lv_style_t *style_get_text_accent(void) { return &style_text_accent; }
lv_style_t *style_get_toggle(void) { return &style_toggle; }
lv_style_t *style_get_slider(void) { return &style_slider; }

lv_color_t color_get_gauge_normal(uint16_t value, uint16_t max)
{
    float ratio = (float)value / max;
    if (ratio < 0.6f) return color_success;
    if (ratio < 0.85f) return color_warning;
    return color_danger;
}

lv_color_t color_get_theme(uint8_t theme_type)
{
    switch (theme_type) {
        case THEME_PRIMARY: return color_primary;
        case THEME_SECONDARY: return color_secondary;
        case THEME_ACCENT: return color_accent;
        case THEME_SUCCESS: return color_success;
        case THEME_WARNING: return color_warning;
        case THEME_DANGER: return color_danger;
        default: return color_text;
    }
}
