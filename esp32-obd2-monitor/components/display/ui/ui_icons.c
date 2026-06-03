#include "ui_icons.h"
#include "styles.h"
#include "board_config.h"

lv_obj_t *ui_icon_create(lv_obj_t *parent, const char *symbol, const lv_font_t *font)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, symbol);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color_accent, 0);
    lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    return lbl;
}

void ui_icon_style(lv_obj_t *label, const lv_font_t *font)
{
    if (label == NULL) {
        return;
    }
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color_accent, 0);
}

ui_wifi_ind_level_t ui_wifi_ind_level_from(bool wifi_ap_up, bool wifi_tcp_up,
                                           connectivity_state_t conn_state)
{
    if (conn_state == CONN_STATE_OBD_READY) {
        return UI_WIFI_IND_OBD;
    }
    if (wifi_tcp_up) {
        return UI_WIFI_IND_TCP;
    }
    if (wifi_ap_up || conn_state == CONN_STATE_LINK_UP ||
        conn_state == CONN_STATE_ELM_INIT) {
        return UI_WIFI_IND_AP;
    }
    return UI_WIFI_IND_OFF;
}

lv_obj_t *ui_wifi_ind_create(lv_obj_t *parent, int x_ofs, int y_ofs)
{
    lv_obj_t *icon = lv_label_create(parent);
    lv_label_set_text(icon, LV_SYMBOL_WIFI);
    lv_obj_align(icon, LV_ALIGN_TOP_RIGHT, x_ofs, y_ofs);
    lv_obj_set_style_text_font(icon, UI_FONT_ICON_LG, 0);
    lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    ui_wifi_ind_apply(icon, UI_WIFI_IND_OFF);
    return icon;
}

void ui_wifi_ind_apply(lv_obj_t *icon, ui_wifi_ind_level_t level)
{
    if (icon == NULL) {
        return;
    }

    lv_obj_remove_flag(icon, LV_OBJ_FLAG_HIDDEN);

    switch (level) {
        case UI_WIFI_IND_OBD:
            lv_obj_set_style_text_color(icon, color_success, 0);
            lv_obj_set_style_opa(icon, LV_OPA_COVER, 0);
            lv_obj_set_style_shadow_width(icon, 20, 0);
            lv_obj_set_style_shadow_color(icon, color_success, 0);
            lv_obj_set_style_shadow_opa(icon, LV_OPA_70, 0);
            lv_obj_set_style_shadow_spread(icon, 2, 0);
            break;
        case UI_WIFI_IND_TCP:
            lv_obj_set_style_text_color(icon, color_accent, 0);
            lv_obj_set_style_opa(icon, LV_OPA_COVER, 0);
            lv_obj_set_style_shadow_width(icon, 14, 0);
            lv_obj_set_style_shadow_color(icon, color_accent, 0);
            lv_obj_set_style_shadow_opa(icon, LV_OPA_50, 0);
            lv_obj_set_style_shadow_spread(icon, 1, 0);
            break;
        case UI_WIFI_IND_AP:
            lv_obj_set_style_text_color(icon, color_primary, 0);
            lv_obj_set_style_opa(icon, LV_OPA_90, 0);
            lv_obj_set_style_shadow_width(icon, 8, 0);
            lv_obj_set_style_shadow_color(icon, color_primary, 0);
            lv_obj_set_style_shadow_opa(icon, LV_OPA_40, 0);
            lv_obj_set_style_shadow_spread(icon, 0, 0);
            break;
        case UI_WIFI_IND_OFF:
        default:
            lv_obj_set_style_text_color(icon, color_text_dim, 0);
            lv_obj_set_style_opa(icon, LV_OPA_40, 0);
            lv_obj_set_style_shadow_width(icon, 0, 0);
            lv_obj_set_style_shadow_spread(icon, 0, 0);
            break;
    }
}
