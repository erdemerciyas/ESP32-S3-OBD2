#include "ui.h"
#include "theme.h"
#include "bsp.h"
#include "vehicle_data.h"

static lv_obj_t *s_tabview;
static lv_obj_t *s_dot_row;
static lv_obj_t *s_dots[UI_TAB_COUNT];
static lv_timer_t *s_update_timer;
static int s_active_tab;
static bool s_was_connected;

static bool adapter_connected(obd_state_t state)
{
    return state == OBD_STATE_ELM_INIT ||
           state == OBD_STATE_PID_DISCOVERY ||
           state == OBD_STATE_READY;
}

static void update_dots(uint32_t active)
{
    const ui_theme_t *t = theme_get();
    for (int i = 0; i < UI_TAB_COUNT; i++) {
        lv_obj_set_style_bg_color(s_dots[i], i == (int)active ? t->primary : t->text_dim, 0);
        lv_obj_set_style_bg_opa(s_dots[i], i == (int)active ? LV_OPA_COVER : LV_OPA_40, 0);
    }
}

static void tab_changed_cb(lv_event_t *e)
{
    (void)e;
    if (!s_tabview) {
        return;
    }
    s_active_tab = (int)lv_tabview_get_tab_act(s_tabview);
    update_dots(lv_tabview_get_tab_act(s_tabview));
}

static void prepare_tab_page(lv_obj_t *tab)
{
    lv_obj_add_flag(tab, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_add_flag(tab, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
}

static void style_tabview(lv_obj_t *tabview)
{
    lv_obj_t *btns = lv_tabview_get_tab_btns(tabview);
    lv_obj_t *cont = lv_tabview_get_content(tabview);

    theme_apply_screen(tabview);
    theme_apply_bg(cont);

    lv_obj_add_flag(btns, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(btns, 0);

    lv_obj_set_scroll_dir(cont, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(cont, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
}

static void ui_update_cb(lv_timer_t *timer)
{
    (void)timer;
    vehicle_data_t *vd = vehicle_data_get();
    bool connected = adapter_connected(vd->state);

    if (connected && !s_was_connected) {
        ui_show_dash();
    }
    s_was_connected = connected;

    s_active_tab = (int)lv_tabview_get_tab_act(s_tabview);
    update_dots(lv_tabview_get_tab_act(s_tabview));

    switch (s_active_tab) {
    case UI_TAB_CONNECT:
        screen_connect_update();
        break;
    case UI_TAB_DASH:
        screen_dash_update(connected);
        break;
    case UI_TAB_GRID:
        screen_grid_update();
        break;
    case UI_TAB_SETTINGS:
        screen_settings_update();
        break;
    default:
        break;
    }
}

void ui_init(void)
{
    bsp_display_lock(-1);

    lv_obj_t *scr = lv_scr_act();
    theme_apply_screen(scr);

    s_tabview = lv_tabview_create(scr, LV_DIR_TOP, UI_TAB_H);
    lv_obj_set_size(s_tabview, UI_VIEWPORT_SZ, UI_VIEWPORT_SZ);
    lv_obj_align(s_tabview, LV_ALIGN_CENTER, 0, 0);
    style_tabview(s_tabview);
    lv_obj_add_event_cb(s_tabview, tab_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *tab_conn = lv_tabview_add_tab(s_tabview, "conn");
    lv_obj_t *tab_dash = lv_tabview_add_tab(s_tabview, "dash");
    lv_obj_t *tab_grid = lv_tabview_add_tab(s_tabview, "grid");
    lv_obj_t *tab_set  = lv_tabview_add_tab(s_tabview, "set");

    lv_obj_t *tabs[] = { tab_conn, tab_dash, tab_grid, tab_set };
    for (int i = 0; i < UI_TAB_COUNT; i++) {
        theme_apply_content(tabs[i]);
        prepare_tab_page(tabs[i]);
    }

    screen_connect_create(tab_conn);
    screen_dash_create(tab_dash);
    screen_grid_create(tab_grid);
    screen_settings_create(tab_set);

    s_dot_row = lv_obj_create(scr);
    lv_obj_set_size(s_dot_row, UI_VIEWPORT_SZ, UI_DOT_H);
    lv_obj_align(s_dot_row, LV_ALIGN_BOTTOM_MID, 0, -UI_DOT_BAR_LIFT);
    lv_obj_set_style_bg_opa(s_dot_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_dot_row, 0, 0);
    lv_obj_set_style_pad_all(s_dot_row, 0, 0);
    lv_obj_set_flex_flow(s_dot_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_dot_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_dot_row, 10, 0);
    lv_obj_clear_flag(s_dot_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_dot_row, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    for (int i = 0; i < UI_TAB_COUNT; i++) {
        s_dots[i] = lv_obj_create(s_dot_row);
        lv_obj_set_size(s_dots[i], 8, 8);
        lv_obj_set_style_radius(s_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(s_dots[i], 0, 0);
    }
    update_dots(UI_TAB_CONNECT);

    bsp_display_unlock();
}

void ui_start_update_timer(void)
{
    s_update_timer = lv_timer_create(ui_update_cb, 50, NULL);
}

void ui_show_dash(void)
{
    if (!s_tabview) {
        return;
    }
    lv_tabview_set_act(s_tabview, UI_TAB_DASH, LV_ANIM_OFF);
    update_dots(UI_TAB_DASH);
}

int ui_get_active_tab(void)
{
    return s_active_tab;
}

bool ui_is_obd_connected(void)
{
    return adapter_connected(vehicle_data_get()->state);
}
