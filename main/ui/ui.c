#include "ui.h"
#include "theme.h"
#include "bsp.h"
#include "vehicle_data.h"
#include "app_mode.h"
#include "clock.h"
#include "nav_state.h"

#define UI_SAVER_IDLE_MS   30000
#define UI_SAVER_HOLD_MS   4000
#define UI_SAVER_HOLD_SLOP 24

static lv_obj_t *s_tabview;
static lv_obj_t *s_dot_row;
static lv_obj_t *s_dots[UI_TAB_COUNT];
static lv_timer_t *s_update_timer;
static lv_obj_t *s_splash_root;
static int s_active_tab;
static bool s_was_connected;
static lv_obj_t *s_home_root;
static lv_obj_t *s_nav_root;
static lv_obj_t *s_roll_root;
static int s_view = -1;

static bool adapter_connected(obd_state_t state)
{
    return state == OBD_STATE_ELM_INIT ||
           state == OBD_STATE_PID_DISCOVERY ||
           state == OBD_STATE_READY;
}

static void splash_finish_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_splash_root) {
        lv_obj_del(s_splash_root);
        s_splash_root = NULL;
    }
    app_mode_t m = app_mode_get();
    ui_show_view(m == APP_MODE_NAV ? UI_VIEW_NAV : m == APP_MODE_ROLL ? UI_VIEW_ROLL : UI_VIEW_OBD);
}

static void home_click_cb(lv_event_t *e)
{
    (void)e;
    ui_show_view(UI_VIEW_HOME);
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

/* Ekranda UI_SAVER_HOLD_MS boyunca kıpırdamadan basılı tutuldu mu? Basış
 * başına bir kez tetikler; arm = false iken süren basış harcanmış sayılır
 * (koruyucuyu kapatan dokunuş uzarsa yeniden açmasın). */
static bool hold_detected(bool arm)
{
    static uint32_t s_t0;
    static lv_point_t s_p0;
    static bool s_down, s_used;
    lv_indev_t *in = lv_indev_get_next(NULL);

    if (!in || in->proc.state != LV_INDEV_STATE_PRESSED) {
        s_down = false;
        return false;
    }
    lv_point_t p;
    lv_indev_get_point(in, &p);
    if (!s_down) {
        s_down = true;
        s_used = false;
        s_t0 = lv_tick_get();
        s_p0 = p;
    }
    if (!arm) {
        s_used = true;
    }
    if (LV_ABS(p.x - s_p0.x) > UI_SAVER_HOLD_SLOP || LV_ABS(p.y - s_p0.y) > UI_SAVER_HOLD_SLOP) {
        s_t0 = lv_tick_get();   /* kaydırma: süre baştan */
        s_p0 = p;
    }
    if (!s_used && lv_tick_elaps(s_t0) >= UI_SAVER_HOLD_MS) {
        s_used = true;
        return true;
    }
    return false;
}

/* Saat ekran koruyucusu: her görünümde 4 sn basılı tutunca açılır. Ayar
 * açıksa NAV dışında 30 sn dokunulmayınca da gelir — NAV'da kendiliğinden
 * asla. Dokunuş kapatır (screen_clock). NAV'da yeni uyarı (radar vb.) gelirse
 * kapanır. true: koruyucu ekranda. */
static bool saver_tick(void)
{
    static uint32_t s_nav_rev;
    static uint32_t s_alert_ts;
    bool hold = hold_detected(!screen_clock_visible());

    if (s_view < 0) {
        return false;   /* açılış animasyonu */
    }
    if (!screen_clock_visible()) {
        bool idle = clock_saver_enabled() && s_view != UI_VIEW_NAV &&
                    lv_disp_get_inactive_time(NULL) >= UI_SAVER_IDLE_MS;
        if (!hold && !idle) {
            return false;
        }
        if (hold) {
            lv_indev_wait_release(lv_indev_get_next(NULL));   /* bırakış alttaki ekrana tıklamasın */
        }
        nav_state_t st;
        nav_state_snapshot(&st);
        s_nav_rev = nav_state_rev();
        s_alert_ts = st.alert.ts_ms;
        screen_clock_show();
    }
    if (s_view == UI_VIEW_NAV && nav_state_rev() != s_nav_rev) {
        s_nav_rev = nav_state_rev();
        nav_state_t st;
        nav_state_snapshot(&st);
        if (st.alert.kind != NAV_ALERT_NONE && st.alert.ts_ms != s_alert_ts) {
            screen_clock_hide();
            lv_disp_trig_activity(NULL);   /* boşta sayacı sıfırla: hemen geri gelmesin */
            return false;
        }
    }
    screen_clock_update();
    return true;
}

static void ui_update_cb(lv_timer_t *timer)
{
    (void)timer;
    if (saver_tick()) {
        return;
    }
    if (s_view == UI_VIEW_NAV) {
        screen_nav_update();
        return;
    }
    if (s_view == UI_VIEW_HOME) {
        screen_home_update();
        return;
    }
    if (s_view == UI_VIEW_ROLL) {
        screen_roll_update();
        return;
    }

    vehicle_data_snapshot_t snap;
    vehicle_data_snapshot(&snap);
    bool connected = adapter_connected(snap.state);

    if (connected && !s_was_connected) {
        ui_show_dash();
    }
    s_was_connected = connected;

    switch (s_active_tab) {
    case UI_TAB_CONNECT:
        screen_connect_update(&snap);
        break;
    case UI_TAB_DASH:
        screen_dash_update(connected, &snap);
        break;
    case UI_TAB_GRID:
        screen_grid_update(&snap);
        break;
    case UI_TAB_DTC:
        screen_dtc_update(&snap);
        break;
    case UI_TAB_GYRO: {
        imu_snapshot_t imu_snap;
        imu_get_snapshot(&imu_snap);
        screen_gyro_update(&imu_snap);
        break;
    }
    case UI_TAB_SETTINGS:
        screen_settings_update(&snap);
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
    lv_obj_add_flag(s_tabview, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *tab_conn = lv_tabview_add_tab(s_tabview, "conn");
    lv_obj_t *tab_dash = lv_tabview_add_tab(s_tabview, "dash");
    lv_obj_t *tab_grid = lv_tabview_add_tab(s_tabview, "grid");
    lv_obj_t *tab_dtc  = lv_tabview_add_tab(s_tabview, "dtc");
    lv_obj_t *tab_gyro = lv_tabview_add_tab(s_tabview, "gyro");
    lv_obj_t *tab_set  = lv_tabview_add_tab(s_tabview, "set");

    lv_obj_t *tabs[] = { tab_conn, tab_dash, tab_grid, tab_dtc, tab_gyro, tab_set };
    for (int i = 0; i < UI_TAB_COUNT; i++) {
        theme_apply_content(tabs[i]);
        prepare_tab_page(tabs[i]);
    }

    screen_connect_create(tab_conn);
    screen_dash_create(tab_dash);
    screen_grid_create(tab_grid);
    screen_dtc_create(tab_dtc);
    screen_gyro_create(tab_gyro);
    screen_settings_create(tab_set);

    s_dot_row = lv_obj_create(scr);
    lv_obj_set_size(s_dot_row, UI_VIEWPORT_SZ, UI_DOT_H + 8);   /* ev simgesi 20 px */
    lv_obj_align(s_dot_row, LV_ALIGN_BOTTOM_MID, 0, -UI_DOT_BAR_LIFT);
    lv_obj_set_style_bg_opa(s_dot_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_dot_row, 0, 0);
    lv_obj_set_style_pad_all(s_dot_row, 0, 0);
    lv_obj_set_flex_flow(s_dot_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_dot_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_dot_row, 10, 0);
    lv_obj_clear_flag(s_dot_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_dot_row, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    lv_obj_t *home = lv_label_create(s_dot_row);
    lv_label_set_text_static(home, LV_SYMBOL_HOME);
    lv_obj_set_style_text_font(home, theme_get()->font_md, 0);
    lv_obj_set_style_text_color(home, theme_get()->primary, 0);
    lv_obj_set_style_pad_hor(home, 8, 0);
    lv_obj_add_flag(home, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(home, 30);   /* küçük simge, büyük dokunma alanı */
    lv_obj_add_flag(s_dot_row, LV_OBJ_FLAG_OVERFLOW_VISIBLE);   /* dokunma alanı satır dışına taşabilsin */
    lv_obj_add_event_cb(home, home_click_cb, LV_EVENT_CLICKED, NULL);

    for (int i = 0; i < UI_TAB_COUNT; i++) {
        s_dots[i] = lv_obj_create(s_dot_row);
        lv_obj_set_size(s_dots[i], 8, 8);
        lv_obj_set_style_radius(s_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(s_dots[i], 0, 0);
    }
    update_dots(UI_TAB_CONNECT);
    lv_obj_add_flag(s_dot_row, LV_OBJ_FLAG_HIDDEN);

    s_home_root = screen_home_create(scr);
    lv_obj_add_flag(s_home_root, LV_OBJ_FLAG_HIDDEN);
    s_nav_root = screen_nav_create(scr);
    lv_obj_add_flag(s_nav_root, LV_OBJ_FLAG_HIDDEN);
    s_roll_root = screen_roll_create(scr);
    lv_obj_add_flag(s_roll_root, LV_OBJ_FLAG_HIDDEN);

    screen_clock_create();

    s_splash_root = screen_splash_create(scr);
    screen_splash_start(s_splash_root, splash_finish_cb);
    ui_shots_start();   /* yalnız CONFIG_UI_SHOT_TOUR derlemesinde etkin */

    bsp_display_unlock();
}

void ui_start_update_timer(void)
{
    s_update_timer = lv_timer_create(ui_update_cb, 16, NULL);
}

void ui_show_tab(int tab)
{
    if (!s_tabview || tab < 0 || tab >= UI_TAB_COUNT) {
        return;
    }
    lv_tabview_set_act(s_tabview, (uint32_t)tab, LV_ANIM_OFF);
    s_active_tab = tab;   /* set_act does not emit VALUE_CHANGED */
    update_dots((uint32_t)tab);
}

static void set_hidden(lv_obj_t *obj, bool hidden)
{
    if (!obj) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void veil_opa_cb(void *obj, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

/* Görünüm geçişi: üst katmanda tam ekran örtü koyudan şeffafa açılır. */
static void transition_veil(void)
{
    lv_obj_t *veil = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(veil);
    lv_obj_set_size(veil, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(veil, theme_get()->bg, 0);
    lv_obj_set_style_bg_opa(veil, LV_OPA_COVER, 0);
    lv_obj_clear_flag(veil, LV_OBJ_FLAG_CLICKABLE);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, veil);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&a, 260);
    lv_anim_set_exec_cb(&a, veil_opa_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&a, lv_obj_del_anim_ready_cb);
    lv_anim_start(&a);
}

void ui_show_view(int view)
{
    if (view == s_view || s_splash_root) {
        return;
    }
    if (s_view >= 0) {
        transition_veil();   /* açılıştan ilk geçişin kendi örtüsü var */
    }
    s_view = view;
    set_hidden(s_tabview, view != UI_VIEW_OBD);
    set_hidden(s_dot_row, view != UI_VIEW_OBD);
    set_hidden(s_home_root, view != UI_VIEW_HOME);
    set_hidden(s_nav_root, view != UI_VIEW_NAV);
    set_hidden(s_roll_root, view != UI_VIEW_ROLL);
}

void ui_show_dash(void)
{
    ui_show_tab(UI_TAB_DASH);
}

int ui_get_active_tab(void)
{
    return s_active_tab;
}

bool ui_is_obd_connected(void)
{
    return adapter_connected(vehicle_data_get()->state);
}
