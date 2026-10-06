#include "ui.h"
#include "theme.h"
#include "vehicle_data.h"
#include "vehicle_profile.h"
#include "obd_link.h"
#include "obd_pids.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Settings — round-panel layout.
 * Five round toggle tiles, three over two (units, link BLE/WiFi, auto
 * connect; centre gauge, profile), title above, a three-line link summary
 * below. Tap a tile to toggle; the profile tile cycles through the stored
 * profiles; the link tile switches the adapter transport and starts its scan. */

#define ST_TILE_D       100
#define ST_TOP_DX       110
#define ST_BOT_DX       56
#define ST_TILE_TOP_Y   (-50)
#define ST_TILE_BOT_Y   62
#define ST_TITLE_Y      (-146)
#define ST_INFO_Y       158
#define ST_INFO_W       260

enum { TILE_UNITS = 0, TILE_LINK, TILE_AUTO, TILE_GAUGE, TILE_PROFILE, TILE_COUNT };

typedef struct {
    lv_obj_t  *obj;
    lv_obj_t  *icon;
    lv_obj_t  *value;
    lv_color_t accent;
} tile_t;

static tile_t s_tiles[TILE_COUNT];
static lv_obj_t *s_info_lbl;

/* Voltage calibration overlay — opened by tapping the info text. Adjusts the
 * shown battery voltage in 0.1 V steps against the adapter's raw reading;
 * stored per transport (BLE / WiFi adapters read differently). */
#define CAL_PANEL_D     400
#define CAL_BTN_D       76
#define CAL_BTN_DX      104
static lv_obj_t *s_cal_panel;
static lv_obj_t *s_cal_link;
static lv_obj_t *s_cal_value;
static lv_obj_t *s_cal_raw;
static char s_prev_info[192] = "";
static char s_prev_active_profile[VEHICLE_PROFILE_ID_LEN] = "";

static const char *state_label(obd_state_t state)
{
    switch (state) {
    case OBD_STATE_SCANNING:      return "Scanning";
    case OBD_STATE_CONNECTING:    return "Connecting";
    case OBD_STATE_ELM_INIT:      return "ELM init";
    case OBD_STATE_PID_DISCOVERY: return "PID discovery";
    case OBD_STATE_READY:         return "Ready";
    case OBD_STATE_ERROR:         return "Error";
    default:                      return "Disconnected";
    }
}

/* Reflect the stored setting on its tile. */
static void tile_refresh(int i)
{
    if (i < 0 || i >= TILE_COUNT) {
        return;
    }
    const ui_theme_t *t = theme_get();
    const vehicle_data_t *vd = vehicle_data_get();
    tile_t *tl = &s_tiles[i];
    bool on = true;

    switch (i) {
    case TILE_UNITS:
        lv_label_set_text_static(tl->value, vd->metric_units ? "km/h \xC2\xB0" "C"
                                                              : "mph \xC2\xB0" "F");
        break;
    case TILE_LINK: {
        bool wifi = obd_link_get_type() == OBD_LINK_WIFI;
        lv_label_set_text_static(tl->icon, wifi ? LV_SYMBOL_WIFI : LV_SYMBOL_BLUETOOTH);
        lv_label_set_text_static(tl->value, wifi ? "WiFi" : "BLE");
        break;
    }
    case TILE_AUTO:
        on = vd->auto_connect;
        lv_label_set_text_static(tl->value, on ? "ON" : "OFF");
        break;
    case TILE_GAUGE:
        lv_label_set_text_static(tl->value, vd->center_gauge_rpm ? "RPM" : "SPEED");
        break;
    case TILE_PROFILE:
        lv_label_set_text(tl->value, vehicle_profile_get()->display_name);
        break;
    default:
        break;
    }

    lv_obj_set_style_border_color(tl->obj, on ? tl->accent : t->border, 0);
    lv_obj_set_style_text_color(tl->icon, on ? tl->accent : t->text_dim, 0);
    lv_obj_set_style_text_color(tl->value, on ? t->text : t->text_dim, 0);
}

static void tile_click_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    vehicle_data_t *vd = vehicle_data_get();

    if (i == TILE_LINK) {
        obd_link_switch(obd_link_get_type() == OBD_LINK_WIFI ? OBD_LINK_BLE : OBD_LINK_WIFI);
    } else if (i == TILE_PROFILE) {
        int count = vehicle_profile_get_count();
        const char *active = vehicle_profile_get_active_id();
        int cur = 0;
        for (int k = 0; k < count; k++) {
            const vehicle_profile_t *p = vehicle_profile_get_at(k);
            if (p && active && strcmp(p->profile_id, active) == 0) {
                cur = k;
                break;
            }
        }
        const vehicle_profile_t *next = count > 0 ? vehicle_profile_get_at((cur + 1) % count) : NULL;
        if (next) {
            vehicle_profile_set_active(next->profile_id);
        }
    } else {
        vehicle_data_lock();
        switch (i) {
        case TILE_UNITS: vd->metric_units = !vd->metric_units;         break;
        case TILE_AUTO:  vd->auto_connect = !vd->auto_connect;         break;
        case TILE_GAUGE: vd->center_gauge_rpm = !vd->center_gauge_rpm; break;
        default:         break;
        }
        vehicle_data_unlock();
    }
    tile_refresh(i);
}

static void create_tile(lv_obj_t *root, int i, lv_coord_t x, lv_coord_t y,
                        const char *icon, const char *name, lv_color_t accent)
{
    const ui_theme_t *t = theme_get();
    tile_t *tl = &s_tiles[i];

    tl->accent = accent;
    tl->obj = lv_obj_create(root);
    theme_apply_lens(tl->obj, ST_TILE_D);
    lv_obj_align(tl->obj, LV_ALIGN_CENTER, x, y);
    lv_obj_set_style_border_width(tl->obj, 2, 0);
    lv_obj_set_style_border_opa(tl->obj, LV_OPA_COVER, 0);
    lv_obj_add_flag(tl->obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tl->obj, tile_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    tl->icon = lv_label_create(tl->obj);
    lv_label_set_text_static(tl->icon, icon);
    lv_obj_set_style_text_font(tl->icon, t->font_md, 0);
    lv_obj_align(tl->icon, LV_ALIGN_CENTER, 0, -24);

    lv_obj_t *nm = lv_label_create(tl->obj);
    lv_label_set_text_static(nm, name);
    lv_obj_set_style_text_font(nm, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(nm, t->text_dim, 0);
    lv_obj_align(nm, LV_ALIGN_CENTER, 0, 0);

    tl->value = lv_label_create(tl->obj);
    lv_label_set_text_static(tl->value, "");
    lv_label_set_long_mode(tl->value, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(tl->value, ST_TILE_D - 24);
    lv_obj_set_style_text_align(tl->value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(tl->value, t->font_sm, 0);
    lv_obj_align(tl->value, LV_ALIGN_CENTER, 0, 22);

    tile_refresh(i);
}

static void cal_refresh(void)
{
    const vehicle_data_t *vd = vehicle_data_get();
    float raw = vd->link.volt_raw;
    float cal = obd_volt_cal_get();

    lv_label_set_text_static(s_cal_link, obd_link_get_type() == OBD_LINK_WIFI ? "WiFi adapter"
                                                                             : "BLE adapter");
    if (raw > 0.1f) {
        lv_label_set_text_fmt(s_cal_value, "%.1f V", raw * cal);
        lv_label_set_text_fmt(s_cal_raw, "%s raw %.2f V  \xE2\x80\xA2  x%.3f",
                              vd->link.volt_src[0] ? vd->link.volt_src : "ATRV", raw, cal);
    } else {
        lv_label_set_text_static(s_cal_value, "-- V");
        lv_label_set_text_fmt(s_cal_raw, "no reading yet  \xE2\x80\xA2  x%.3f", cal);
    }
}

static void cal_step_cb(lv_event_t *e)
{
    int step = (int)(intptr_t)lv_event_get_user_data(e);   /* -1 / +1 = 0.1 V, 0 = reset */
    float raw = vehicle_data_get()->link.volt_raw;

    if (step == 0) {
        obd_volt_cal_set(1.0f);
    } else if (raw > 0.1f) {
        float target = (roundf(raw * obd_volt_cal_get() * 10.0f) + step) / 10.0f;
        obd_volt_cal_set(target / raw);
    }
    cal_refresh();
}

static void cal_open_cb(lv_event_t *e)
{
    (void)e;
    cal_refresh();
    lv_obj_clear_flag(s_cal_panel, LV_OBJ_FLAG_HIDDEN);
}

static void cal_close_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_cal_panel, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *cal_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, lv_coord_t y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text_static(l, "");
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, y);
    return l;
}

static void cal_button(lv_obj_t *parent, lv_coord_t x, const char *text, int step)
{
    const ui_theme_t *t = theme_get();
    lv_obj_t *b = lv_obj_create(parent);
    theme_apply_lens(b, CAL_BTN_D);
    lv_obj_align(b, LV_ALIGN_CENTER, x, 70);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, cal_step_cb, LV_EVENT_CLICKED, (void *)(intptr_t)step);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text_static(l, text);
    lv_obj_set_style_text_font(l, step ? t->font_md : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, step ? t->primary : t->text_dim, 0);
    lv_obj_center(l);
}

static void cal_create(lv_obj_t *root)
{
    const ui_theme_t *t = theme_get();

    s_cal_panel = lv_obj_create(root);
    theme_apply_lens(s_cal_panel, CAL_PANEL_D);
    lv_obj_set_style_bg_color(s_cal_panel, t->bg, 0);
    lv_obj_set_style_bg_opa(s_cal_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_cal_panel, t->primary, 0);
    lv_obj_set_style_border_width(s_cal_panel, 2, 0);
    lv_obj_set_style_border_opa(s_cal_panel, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_cal_panel, LV_OBJ_FLAG_CLICKABLE);   /* dokunuşlar alttaki karolara geçmesin */
    lv_obj_add_flag(s_cal_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *title = cal_label(s_cal_panel, &lv_font_montserrat_14, t->text_dim, -122);
    lv_label_set_text_static(title, "BATTERY CALIBRATION");
    lv_obj_set_style_text_letter_space(title, 2, 0);

    s_cal_link  = cal_label(s_cal_panel, t->font_sm, t->secondary, -96);
    s_cal_value = cal_label(s_cal_panel, t->font_xl, t->text, -44);
    s_cal_raw   = cal_label(s_cal_panel, &lv_font_montserrat_14, t->text_dim, 4);

    cal_button(s_cal_panel, -CAL_BTN_DX, LV_SYMBOL_MINUS, -1);
    cal_button(s_cal_panel, 0, "RESET", 0);
    cal_button(s_cal_panel, CAL_BTN_DX, LV_SYMBOL_PLUS, 1);

    lv_obj_t *done = cal_label(s_cal_panel, t->font_md, t->primary, 146);
    lv_label_set_text_static(done, "DONE");
    lv_obj_add_flag(done, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(done, 16);
    lv_obj_add_event_cb(done, cal_close_cb, LV_EVENT_CLICKED, NULL);
}

void screen_settings_create(lv_obj_t *parent)
{
    const ui_theme_t *t = theme_get();
    lv_obj_t *root = theme_create_root(parent);

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text_static(title, "SETTINGS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_letter_space(title, 3, 0);
    lv_obj_set_style_text_color(title, t->text_dim, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, ST_TITLE_Y);

    create_tile(root, TILE_UNITS,   -ST_TOP_DX, ST_TILE_TOP_Y, LV_SYMBOL_SHUFFLE,   "Units",   t->primary);
    create_tile(root, TILE_LINK,     0,         ST_TILE_TOP_Y, LV_SYMBOL_BLUETOOTH, "Link",    t->primary);
    create_tile(root, TILE_AUTO,     ST_TOP_DX, ST_TILE_TOP_Y, LV_SYMBOL_LOOP,      "Auto",    t->ok);
    create_tile(root, TILE_GAUGE,   -ST_BOT_DX, ST_TILE_BOT_Y, LV_SYMBOL_REFRESH,   "Centre",  t->secondary);
    create_tile(root, TILE_PROFILE,  ST_BOT_DX, ST_TILE_BOT_Y, LV_SYMBOL_LIST,      "Profile", t->warn);

    s_info_lbl = lv_label_create(root);
    lv_label_set_text_static(s_info_lbl, "");
    lv_obj_set_width(s_info_lbl, ST_INFO_W);
    lv_label_set_long_mode(s_info_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_info_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_info_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_info_lbl, t->text_dim, 0);
    lv_obj_align(s_info_lbl, LV_ALIGN_CENTER, 0, ST_INFO_Y);
    lv_obj_add_flag(s_info_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_info_lbl, 8);
    lv_obj_add_event_cb(s_info_lbl, cal_open_cb, LV_EVENT_CLICKED, NULL);

    cal_create(root);   /* last: drawn above the tiles */
}

void screen_settings_update(const vehicle_data_snapshot_t *snap)
{
    const vehicle_profile_t *profile = vehicle_profile_get();
    char buf[192];

    /* Profile may change from elsewhere (NVS load, auto-detect). */
    if (strcmp(s_prev_active_profile, profile->profile_id) != 0) {
        strncpy(s_prev_active_profile, profile->profile_id, sizeof(s_prev_active_profile) - 1);
        s_prev_active_profile[sizeof(s_prev_active_profile) - 1] = '\0';
        tile_refresh(TILE_PROFILE);
    }

    static uint32_t s_cal_tick;
    if (!lv_obj_has_flag(s_cal_panel, LV_OBJ_FLAG_HIDDEN) && lv_tick_elaps(s_cal_tick) > 500) {
        s_cal_tick = lv_tick_get();
        cal_refresh();   /* live raw reading while calibrating */
    }

    char volt[32] = "--";
    if (snap->link.volt_raw > 0.1f) {
        /* Ham adaptör okuması + kaynağı: kalibrasyon/aşırı şarj teşhisi için. */
        snprintf(volt, sizeof(volt), "%.1fV (%s %.2f)", snap->voltage,
                 snap->link.volt_src, snap->link.volt_raw);
    } else if (snap->voltage > 0.1f) {
        snprintf(volt, sizeof(volt), "%.1fV", snap->voltage);
    }
    const char *adapter = snap->link.elm_id[0] ? snap->link.elm_id
                        : snap->adapter_name[0] ? snap->adapter_name : "No adapter";
    snprintf(buf, sizeof(buf),
             "%s  \xE2\x80\xA2  %s\n%s%s  \xE2\x80\xA2  %s\n%.1f req/s  \xE2\x80\xA2  RPM %.1f Hz  \xE2\x80\xA2  TO %lu",
             adapter, state_label(snap->state),
             snap->link.proto[0] ? snap->link.proto : profile->protocol,
             snap->link.resp_count ? " (x1)" : "",
             volt,
             snap->link.req_rate,
             snap->link.rpm_hz,
             (unsigned long)snap->link.timeouts);
    if (strcmp(buf, s_prev_info) != 0) {
        snprintf(s_prev_info, sizeof(s_prev_info), "%s", buf);
        lv_label_set_text(s_info_lbl, buf);
    }
}
