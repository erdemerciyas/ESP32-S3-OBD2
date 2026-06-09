#include "ui_icons.h"
#include "styles.h"
#include "board_config.h"
#include <stdint.h>

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

ui_conn_ind_level_t ui_conn_ind_level_from_ex(bool link_up, bool serial_up,
                                              connectivity_state_t conn_state,
                                              bool auto_connect_pending)
{
    if (conn_state == CONN_STATE_OBD_READY) {
        return UI_CONN_IND_OBD;
    }
    if (serial_up) {
        return UI_CONN_IND_SERIAL;
    }
    if (auto_connect_pending) {
        return UI_CONN_IND_PENDING;
    }
    if (link_up || conn_state == CONN_STATE_LINK_UP ||
        conn_state == CONN_STATE_ELM_INIT) {
        return UI_CONN_IND_LINK;
    }
    return UI_CONN_IND_OFF;
}

ui_conn_ind_level_t ui_conn_ind_level_from(bool link_up, bool serial_up,
                                           connectivity_state_t conn_state)
{
    return ui_conn_ind_level_from_ex(link_up, serial_up, conn_state, false);
}

lv_obj_t *ui_conn_ind_create(lv_obj_t *parent, int x_ofs, int y_ofs)
{
    lv_obj_t *icon = lv_label_create(parent);
    lv_label_set_text(icon, LV_SYMBOL_BLUETOOTH);
    lv_obj_align(icon, LV_ALIGN_TOP_RIGHT, x_ofs, y_ofs);
    lv_obj_set_style_text_font(icon, UI_FONT_ICON_LG, 0);
    lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    ui_conn_ind_apply(icon, UI_CONN_IND_OFF);
    return icon;
}

lv_obj_t *ui_conn_ind_create_gauge_hud(lv_obj_t *parent)
{
    lv_obj_t *icon = lv_label_create(parent);
    lv_label_set_text(icon, LV_SYMBOL_BLUETOOTH);
    lv_obj_align(icon, LV_ALIGN_BOTTOM_MID, 0, UI_GAUGE_CONN_ICON_Y_OFS);
    lv_obj_set_style_text_font(icon, UI_FONT_ICON, 0);
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    ui_conn_ind_apply(icon, UI_CONN_IND_OFF);
    return icon;
}

void ui_conn_ind_apply(lv_obj_t *icon, ui_conn_ind_level_t level)
{
    if (icon == NULL) {
        return;
    }

    uint32_t pulse_phase = 0;
    if (level == UI_CONN_IND_PENDING) {
        pulse_phase = lv_tick_get() / 450U;
    }
    const uint32_t packed = ((uint32_t)level << 8) | (pulse_phase & 0xFFU);
    if ((uint32_t)(uintptr_t)lv_obj_get_user_data(icon) == packed) {
        return;
    }
    lv_obj_set_user_data(icon, (void *)(uintptr_t)packed);

    lv_obj_remove_flag(icon, LV_OBJ_FLAG_HIDDEN);

    switch (level) {
        case UI_CONN_IND_OBD:
            lv_obj_set_style_text_color(icon, color_success, 0);
            lv_obj_set_style_opa(icon, LV_OPA_COVER, 0);
            lv_obj_set_style_shadow_width(icon, 20, 0);
            lv_obj_set_style_shadow_color(icon, color_success, 0);
            lv_obj_set_style_shadow_opa(icon, LV_OPA_70, 0);
            lv_obj_set_style_shadow_spread(icon, 2, 0);
            break;
        case UI_CONN_IND_SERIAL:
            lv_obj_set_style_text_color(icon, color_accent, 0);
            lv_obj_set_style_opa(icon, LV_OPA_COVER, 0);
            lv_obj_set_style_shadow_width(icon, 14, 0);
            lv_obj_set_style_shadow_color(icon, color_accent, 0);
            lv_obj_set_style_shadow_opa(icon, LV_OPA_50, 0);
            lv_obj_set_style_shadow_spread(icon, 1, 0);
            break;
        case UI_CONN_IND_PENDING: {
            const lv_opa_t pulse = ((lv_tick_get() / 450U) % 2U) ? LV_OPA_40 : LV_OPA_COVER;
            lv_obj_set_style_text_color(icon, color_warning, 0);
            lv_obj_set_style_opa(icon, pulse, 0);
            lv_obj_set_style_shadow_width(icon, 14, 0);
            lv_obj_set_style_shadow_color(icon, color_warning, 0);
            lv_obj_set_style_shadow_opa(icon, LV_OPA_50, 0);
            lv_obj_set_style_shadow_spread(icon, 1, 0);
            break;
        }
        case UI_CONN_IND_LINK:
            lv_obj_set_style_text_color(icon, color_primary, 0);
            lv_obj_set_style_opa(icon, LV_OPA_90, 0);
            lv_obj_set_style_shadow_width(icon, 8, 0);
            lv_obj_set_style_shadow_color(icon, color_primary, 0);
            lv_obj_set_style_shadow_opa(icon, LV_OPA_40, 0);
            lv_obj_set_style_shadow_spread(icon, 0, 0);
            break;
        case UI_CONN_IND_OFF:
        default:
            lv_obj_set_style_text_color(icon, color_text_dim, 0);
            lv_obj_set_style_opa(icon, LV_OPA_40, 0);
            lv_obj_set_style_shadow_width(icon, 0, 0);
            lv_obj_set_style_shadow_spread(icon, 0, 0);
            break;
    }
}
