#include "dashboard.h"
#include "gauge.h"
#include "styles.h"
#include "obd_service.h"
#include "esp_log.h"

static const char *TAG = "dashboard";

/* Screen objects */
static lv_obj_t *screen_dashboard;
static lv_obj_t *screen_menu;
static lv_obj_t *screen_settings;
static lv_obj_t *screen_connection;
static lv_obj_t *screen_about;

static lv_obj_t *current_screen;

/* Connection status indicators */
static lv_obj_t *conn_wifi_indicator;
static lv_obj_t *conn_bt_indicator;
static lv_obj_t *conn_usb_indicator;
static uint8_t active_connection = 0; /* 0=WIFI, 1=BT, 2=USB */

/* Info panel labels */
static lv_obj_t *info_conn_label;
static lv_obj_t *info_obd_label;
static lv_obj_t *info_ecu_label;
static lv_obj_t *info_mode_label;

/* DTC warning indicator */
static lv_obj_t *dtc_warning_label;

static void create_header(lv_obj_t *parent);
static void create_connection_status(lv_obj_t *parent);
static void create_dashboard_screen(void);
static void create_menu_screen(void);
static void create_settings_screen(void);
static void create_connection_screen(void);
static void create_about_screen(void);
static void update_connection_indicator(uint8_t type);

void dashboard_init(void)
{
    ESP_LOGI(TAG, "Initializing racing dashboard...");

    styles_init();

    create_dashboard_screen();
    create_menu_screen();
    create_settings_screen();
    create_connection_screen();
    create_about_screen();

    dashboard_show_screen(DASHBOARD_MAIN);

    ESP_LOGI(TAG, "Racing dashboard initialized");
}

void dashboard_show_screen(dashboard_screen_t screen)
{
    switch (screen) {
        case DASHBOARD_MAIN:
            current_screen = screen_dashboard;
            break;
        case DASHBOARD_MENU:
            current_screen = screen_menu;
            break;
        case DASHBOARD_SETTINGS:
            current_screen = screen_settings;
            break;
        case DASHBOARD_CONNECTION:
            current_screen = screen_connection;
            break;
        case DASHBOARD_ABOUT:
            current_screen = screen_about;
            break;
        default:
            return;
    }

    lv_scr_load(current_screen);
}

void display_update_gauges(void)
{
    obd_data_t data;
    obd_service_get_data(&data);

    /* Update all 10 gauges matching ui-demo.html */
    if (data.rpm_valid) {
        gauge_update_fullscreen(GAUGE_RPM, (int16_t)data.rpm);
    }

    if (data.speed_valid) {
        gauge_update_fullscreen(GAUGE_SPEED, (int16_t)data.speed);
    }

    if (data.coolant_valid) {
        gauge_update_fullscreen(GAUGE_COOLANT, (int16_t)data.coolant_temp);
    }

    if (data.throttle_valid) {
        gauge_update_fullscreen(GAUGE_THROTTLE, (int16_t)data.throttle_pos);
    }

    if (data.fuel_valid) {
        gauge_update_fullscreen(GAUGE_FUEL, (int16_t)data.fuel_level);
    }

    if (data.load_valid) {
        gauge_update_fullscreen(GAUGE_LOAD, (int16_t)data.engine_load);
    }

    /* Battery voltage - estimate from OBD data or set default */
    if (data.rpm_valid) {
        /* Estimate battery voltage: 12.6V base + charging when RPM > 1000 */
        int16_t voltage = 126; /* 12.6V * 10 */
        if (data.rpm > 1000) {
            voltage = 138 + (rand() % 10); /* 13.8-14.7V when charging */
        }
        gauge_update_fullscreen(GAUGE_VOLTAGE, voltage);
    }

    /* Intake temperature - from OBD or estimate from coolant temp */
    if (data.intake_valid) {
        gauge_update_fullscreen(GAUGE_INTAKE, (int16_t)data.intake_temp);
    } else if (data.coolant_valid) {
        int16_t intake_temp = data.coolant_temp - 10; /* Intake typically cooler */
        if (intake_temp < 20) intake_temp = 20;
        gauge_update_fullscreen(GAUGE_INTAKE, intake_temp);
    }

    /* Fuel consumption (L/h) - multiply by 10 for display */
    if (data.fuel_consumption_valid) {
        int16_t fuel_rate = (int16_t)(data.fuel_consumption * 10); /* 8.5 L/h -> 85 */
        gauge_update_fullscreen(GAUGE_FUEL_CONSUMPTION, fuel_rate);
    }

    /* DTC warning - show count of error codes */
    if (data.dtc_present) {
        gauge_update_fullscreen(GAUGE_DTC_WARNING, (int16_t)data.dtc_count);
        /* Show DTC warning indicator in status bar */
        if (dtc_warning_label) {
            lv_obj_clear_flag(dtc_warning_label, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        gauge_update_fullscreen(GAUGE_DTC_WARNING, 0);
        /* Hide DTC warning indicator */
        if (dtc_warning_label) {
            lv_obj_add_flag(dtc_warning_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void create_header(lv_obj_t *parent)
{
    /* Header container */
    lv_obj_t *header = lv_obj_create(parent);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, 480, 60);
    lv_obj_set_style_bg_color(header, color_card_bg, 0);
    lv_obj_set_style_border_color(header, color_card_border, 0);
    lv_obj_set_style_border_width(header, 0, 0);

    /* Title: OBD2 RACING */
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "OBD2 RACING");
    lv_obj_set_pos(title, 120, 15);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, color_primary, 0);

    /* Subtitle */
    lv_obj_t *subtitle = lv_label_create(header);
    lv_label_set_text(subtitle, "PERFORMANCE MONITORING");
    lv_obj_set_pos(subtitle, 130, 38);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(subtitle, color_accent, 0);
}

static void create_connection_status(lv_obj_t *parent)
{
    /* Connection status container */
    lv_obj_t *conn_container = lv_obj_create(parent);
    lv_obj_set_pos(conn_container, 0, 65);
    lv_obj_set_size(conn_container, 480, 35);
    lv_obj_set_style_bg_color(conn_container, color_bg_dark, 0);
    lv_obj_set_style_border_color(conn_container, color_card_border, 0);
    lv_obj_set_style_border_width(conn_container, 0, 0);

    /* Center align connection indicators */
    lv_obj_t *conn_wifi = lv_obj_create(conn_container);
    lv_obj_set_pos(conn_wifi, 130, 5);
    lv_obj_set_size(conn_wifi, 70, 25);
    lv_obj_set_style_bg_color(conn_wifi, color_card_bg, 0);
    lv_obj_set_style_border_color(conn_wifi, color_primary, 0);
    lv_obj_set_style_border_width(conn_wifi, 1, 0);
    lv_obj_set_style_radius(conn_wifi, 12, 0);
    conn_wifi_indicator = conn_wifi;

    lv_obj_t *wifi_dot = lv_obj_create(conn_wifi);
    lv_obj_set_pos(wifi_dot, 8, 10);
    lv_obj_set_size(wifi_dot, 6, 6);
    lv_obj_set_style_bg_color(wifi_dot, color_primary, 0);
    lv_obj_set_style_radius(wifi_dot, 3, 0);

    lv_obj_t *wifi_label = lv_label_create(conn_wifi);
    lv_label_set_text(wifi_label, "WiFi");
    lv_obj_set_pos(wifi_label, 20, 7);
    lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(wifi_label, color_primary, 0);

    /* Bluetooth indicator */
    lv_obj_t *conn_bt = lv_obj_create(conn_container);
    lv_obj_set_pos(conn_bt, 205, 5);
    lv_obj_set_size(conn_bt, 70, 25);
    lv_obj_set_style_bg_color(conn_bt, color_card_bg, 0);
    lv_obj_set_style_border_color(conn_bt, color_card_border, 0);
    lv_obj_set_style_border_width(conn_bt, 1, 0);
    lv_obj_set_style_radius(conn_bt, 12, 0);
    conn_bt_indicator = conn_bt;

    lv_obj_t *bt_dot = lv_obj_create(conn_bt);
    lv_obj_set_pos(bt_dot, 8, 10);
    lv_obj_set_size(bt_dot, 6, 6);
    lv_obj_set_style_bg_color(bt_dot, color_text_dim, 0);
    lv_obj_set_style_radius(bt_dot, 3, 0);

    lv_obj_t *bt_label = lv_label_create(conn_bt);
    lv_label_set_text(bt_label, "BT");
    lv_obj_set_pos(bt_label, 22, 7);
    lv_obj_set_style_text_font(bt_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(bt_label, color_text_dim, 0);

    /* USB indicator */
    lv_obj_t *conn_usb = lv_obj_create(conn_container);
    lv_obj_set_pos(conn_usb, 280, 5);
    lv_obj_set_size(conn_usb, 70, 25);
    lv_obj_set_style_bg_color(conn_usb, color_card_bg, 0);
    lv_obj_set_style_border_color(conn_usb, color_card_border, 0);
    lv_obj_set_style_border_width(conn_usb, 1, 0);
    lv_obj_set_style_radius(conn_usb, 12, 0);
    conn_usb_indicator = conn_usb;

    lv_obj_t *usb_dot = lv_obj_create(conn_usb);
    lv_obj_set_pos(usb_dot, 8, 10);
    lv_obj_set_size(usb_dot, 6, 6);
    lv_obj_set_style_bg_color(usb_dot, color_text_dim, 0);
    lv_obj_set_style_radius(usb_dot, 3, 0);

    lv_obj_t *usb_label = lv_label_create(conn_usb);
    lv_label_set_text(usb_label, "USB");
    lv_obj_set_pos(usb_label, 20, 7);
    lv_obj_set_style_text_font(usb_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(usb_label, color_text_dim, 0);

    /* DTC Warning indicator - hidden by default */
    lv_obj_t *conn_dtc = lv_obj_create(conn_container);
    lv_obj_set_pos(conn_dtc, 355, 5);
    lv_obj_set_size(conn_dtc, 110, 25);
    lv_obj_set_style_bg_color(conn_dtc, LV_COLOR_MAKE(0x40, 0x10, 0x10), 0);
    lv_obj_set_style_border_color(conn_dtc, color_danger, 0);
    lv_obj_set_style_border_width(conn_dtc, 1, 0);
    lv_obj_set_style_radius(conn_dtc, 12, 0);
    lv_obj_add_flag(conn_dtc, LV_OBJ_FLAG_HIDDEN); /* Hidden initially */
    dtc_warning_label = conn_dtc;

    lv_obj_t *dtc_dot = lv_obj_create(conn_dtc);
    lv_obj_set_pos(dtc_dot, 8, 10);
    lv_obj_set_size(dtc_dot, 6, 6);
    lv_obj_set_style_bg_color(dtc_dot, color_danger, 0);
    lv_obj_set_style_radius(dtc_dot, 3, 0);

    lv_obj_t *dtc_lbl = lv_label_create(conn_dtc);
    lv_label_set_text(dtc_lbl, "CHECK ENG");
    lv_obj_set_pos(dtc_lbl, 20, 7);
    lv_obj_set_style_text_font(dtc_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(dtc_lbl, color_danger, 0);
}

static void update_connection_indicator(uint8_t type)
{
    active_connection = type;

    /* Reset all */
    lv_obj_set_style_border_color(conn_wifi_indicator, color_card_border, 0);
    lv_obj_set_style_bg_color(conn_wifi_indicator, color_card_bg, 0);
    lv_obj_set_style_border_color(conn_bt_indicator, color_card_border, 0);
    lv_obj_set_style_bg_color(conn_bt_indicator, color_card_bg, 0);
    lv_obj_set_style_border_color(conn_usb_indicator, color_card_border, 0);
    lv_obj_set_style_bg_color(conn_usb_indicator, color_card_bg, 0);

    /* Highlight active */
    lv_obj_t *active_indicator = NULL;
    switch (type) {
        case 0: active_indicator = conn_wifi_indicator; break;
        case 1: active_indicator = conn_bt_indicator; break;
        case 2: active_indicator = conn_usb_indicator; break;
    }

    if (active_indicator) {
        lv_obj_set_style_border_color(active_indicator, color_primary, 0);
        lv_obj_set_style_bg_color(active_indicator, LV_COLOR_MAKE(0x40, 0x10, 0x10), 0);
    }
}

/* Gauge navigation functions */
void dashboard_navigate_gauge_next(void)
{
    gauge_type_t current = gauge_get_active();
    gauge_type_t next = (current + 1) % GAUGE_MAX;
    gauge_set_active(next);
    ESP_LOGI(TAG, "Gauge navigated to: %d", next);
}

void dashboard_navigate_gauge_prev(void)
{
    gauge_type_t current = gauge_get_active();
    gauge_type_t prev = (current == 0) ? (GAUGE_MAX - 1) : (current - 1);
    gauge_set_active(prev);
    ESP_LOGI(TAG, "Gauge navigated to: %d", prev);
}

static void create_dashboard_screen(void)
{
    screen_dashboard = lv_obj_create(NULL);
    lv_obj_add_style(screen_dashboard, style_get_bg_dark(), 0);
    lv_obj_set_style_bg_color(screen_dashboard, color_bg_dark, 0);

    /* Header */
    create_header(screen_dashboard);

    /* Connection Status */
    create_connection_status(screen_dashboard);

    /* Full-screen gauge with swipe navigation - matching ui-demo.html */
    gauge_create_fullscreen(screen_dashboard, GAUGE_RPM);
    
    /* Enable gesture recognition on the gauge container */
    lv_obj_add_flag(screen_dashboard, LV_OBJ_FLAG_GESTURE_BUBBLE);
    
    /* Swipe hint label */
    lv_obj_t *swipe_hint = lv_label_create(screen_dashboard);
    lv_label_set_text(swipe_hint, "< SWIPE TO CHANGE >");
    lv_obj_align(swipe_hint, LV_ALIGN_BOTTOM_MID, 0, -15);
    lv_obj_set_style_text_font(swipe_hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(swipe_hint, color_text_dim, 0);
    lv_obj_set_style_text_letter_space(swipe_hint, 3, 0);
    lv_obj_set_style_opa(swipe_hint, LV_OPA_50, 0);
    
    ESP_LOGI(TAG, "Dashboard screen created with fullscreen gauge");
}

static void create_menu_screen(void)
{
    screen_menu = lv_obj_create(NULL);
    lv_obj_add_style(screen_menu, style_get_bg_dark(), 0);
    lv_obj_set_style_bg_color(screen_menu, color_bg_dark, 0);

    /* Header */
    lv_obj_t *header = lv_obj_create(screen_menu);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, 480, 60);
    lv_obj_set_style_bg_color(header, color_card_bg, 0);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "MENU");
    lv_obj_set_pos(title, 200, 20);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, color_primary, 0);

    /* Menu items grid - 2x2 */
    const char *menu_titles[] = {"Gosterge", "Baglanti", "Ayarlar", "Hakkinda"};
    const char *menu_descs[] = {"Arac bilgilerini gorsellestir", "WiFi/BT/USB ayarlari", "Sistem konfigurasyonu", "Surum ve bilgiler"};

    for (int i = 0; i < 4; i++) {
        int row = i / 2;
        int col = i % 2;
        int x = 30 + col * 210;
        int y = 80 + row * 120;

        lv_obj_t *card = lv_obj_create(screen_menu);
        lv_obj_set_pos(card, x, y);
        lv_obj_set_size(card, 200, 100);
        lv_obj_set_style_bg_color(card, color_card_bg, 0);
        lv_obj_set_style_border_color(card, color_card_border, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, 15, 0);

        /* Icon placeholder */
        lv_obj_t *icon_bg = lv_obj_create(card);
        lv_obj_set_pos(icon_bg, 75, 15);
        lv_obj_set_size(icon_bg, 50, 50);
        lv_obj_set_style_bg_color(icon_bg, color_primary, 0);
        lv_obj_set_style_radius(icon_bg, 12, 0);

        lv_obj_t *title_lbl = lv_label_create(card);
        lv_label_set_text(title_lbl, menu_titles[i]);
        lv_obj_set_pos(title_lbl, 60, 70);
        lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(title_lbl, color_text, 0);

        lv_obj_t *desc_lbl = lv_label_create(card);
        lv_label_set_text(desc_lbl, menu_descs[i]);
        lv_obj_set_pos(desc_lbl, 25, 85);
        lv_obj_set_style_text_font(desc_lbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(desc_lbl, color_text_dim, 0);
    }

    /* Back button */
    lv_obj_t *btn_back = lv_btn_create(screen_menu);
    lv_obj_set_pos(btn_back, 200, 330);
    lv_obj_set_size(btn_back, 80, 35);
    lv_obj_set_style_bg_color(btn_back, color_primary, 0);
    lv_obj_set_style_radius(btn_back, 17, 0);

    lv_obj_t *lbl_back = lv_label_create(btn_back, "GERI");
    lv_obj_set_style_text_color(lbl_back, color_text, 0);
    lv_obj_set_style_text_font(lbl_back, &lv_font_montserrat_12, 0);
}

static void create_settings_screen(void)
{
    screen_settings = lv_obj_create(NULL);
    lv_obj_add_style(screen_settings, style_get_bg_dark(), 0);
    lv_obj_set_style_bg_color(screen_settings, color_bg_dark, 0);

    /* Header */
    lv_obj_t *header = lv_obj_create(screen_settings);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, 480, 60);
    lv_obj_set_style_bg_color(header, color_card_bg, 0);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "AYARLAR");
    lv_obj_set_pos(title, 190, 20);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, color_primary, 0);

    /* Settings groups */
    lv_obj_t *group_display = lv_obj_create(screen_settings);
    lv_obj_set_pos(group_display, 20, 75);
    lv_obj_set_size(group_display, 440, 110);
    lv_obj_set_style_bg_color(group_display, color_card_bg, 0);
    lv_obj_set_style_border_color(group_display, color_card_border, 0);
    lv_obj_set_style_border_width(group_display, 1, 0);
    lv_obj_set_style_radius(group_display, 15, 0);

    lv_obj_t *group_title1 = lv_label_create(group_display);
    lv_label_set_text(group_title1, "DISPLAY");
    lv_obj_set_pos(group_title1, 15, 10);
    lv_obj_set_style_text_font(group_title1, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(group_title1, color_primary, 0);

    /* Brightness slider */
    lv_obj_t *brightness_lbl = lv_label_create(group_display);
    lv_label_set_text(brightness_lbl, "Parlaklik");
    lv_obj_set_pos(brightness_lbl, 15, 35);
    lv_obj_set_style_text_color(brightness_lbl, color_text, 0);

    lv_obj_t *brightness_bar = lv_slider_create(group_display);
    lv_obj_set_pos(brightness_bar, 120, 38);
    lv_obj_set_size(brightness_bar, 200, 10);
    lv_slider_set_range(brightness_bar, 0, 100);
    lv_slider_set_value(brightness_bar, 70, LV_ANIM_ON);
    lv_obj_set_style_bg_color(brightness_bar, color_card_border, 0);
    lv_obj_set_style_bg_color(brightness_bar, color_primary, LV_PART_INDICATOR);

    lv_obj_t *brightness_val = lv_label_create(group_display);
    lv_label_set_text(brightness_val, "70%");
    lv_obj_set_pos(brightness_val, 330, 35);
    lv_obj_set_style_text_color(brightness_val, color_primary, 0);

    /* Night mode toggle */
    lv_obj_t *night_lbl = lv_label_create(group_display);
    lv_label_set_text(night_lbl, "Gece Modu");
    lv_obj_set_pos(night_lbl, 15, 65);
    lv_obj_set_style_text_color(night_lbl, color_text, 0);

    lv_obj_t *night_toggle = lv_switch_create(group_display);
    lv_obj_set_pos(night_toggle, 320, 65);
    lv_switch_on(night_toggle, LV_ANIM_ON);
    lv_obj_set_style_bg_color(night_toggle, color_primary, LV_PART_INDICATOR);

    /* Alerts group */
    lv_obj_t *group_alerts = lv_obj_create(screen_settings);
    lv_obj_set_pos(group_alerts, 20, 195);
    lv_obj_set_size(group_alerts, 440, 110);
    lv_obj_set_style_bg_color(group_alerts, color_card_bg, 0);
    lv_obj_set_style_border_color(group_alerts, color_card_border, 0);
    lv_obj_set_style_border_width(group_alerts, 1, 0);
    lv_obj_set_style_radius(group_alerts, 15, 0);

    lv_obj_t *group_title2 = lv_label_create(group_alerts);
    lv_label_set_text(group_title2, "ALERTS");
    lv_obj_set_pos(group_title2, 15, 10);
    lv_obj_set_style_text_font(group_title2, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(group_title2, color_primary, 0);

    /* Alert toggles */
    lv_obj_t *rpm_lbl = lv_label_create(group_alerts);
    lv_label_set_text(rpm_lbl, "RPM Uyarisi");
    lv_obj_set_pos(rpm_lbl, 15, 40);
    lv_obj_set_style_text_color(rpm_lbl, color_text, 0);

    lv_obj_t *rpm_toggle = lv_switch_create(group_alerts);
    lv_obj_set_pos(rpm_toggle, 320, 40);
    lv_switch_on(rpm_toggle, LV_ANIM_ON);
    lv_obj_set_style_bg_color(rpm_toggle, color_primary, LV_PART_INDICATOR);

    lv_obj_t *temp_lbl = lv_label_create(group_alerts);
    lv_label_set_text(temp_lbl, "Sicaklik Uyarisi");
    lv_obj_set_pos(temp_lbl, 15, 70);
    lv_obj_set_style_text_color(temp_lbl, color_text, 0);

    lv_obj_t *temp_toggle = lv_switch_create(group_alerts);
    lv_obj_set_pos(temp_toggle, 320, 70);
    lv_switch_on(temp_toggle, LV_ANIM_ON);
    lv_obj_set_style_bg_color(temp_toggle, color_primary, LV_PART_INDICATOR);

    /* Reset button */
    lv_obj_t *btn_reset = lv_btn_create(screen_settings);
    lv_obj_set_pos(btn_reset, 140, 320);
    lv_obj_set_size(btn_reset, 200, 40);
    lv_obj_set_style_bg_color(btn_reset, LV_COLOR_MAKE(0x00, 0x00, 0x00), 0);
    lv_obj_set_style_border_color(btn_reset, color_danger, 0);
    lv_obj_set_style_border_width(btn_reset, 1, 0);
    lv_obj_set_style_radius(btn_reset, 12, 0);

    lv_obj_t *lbl_reset = lv_label_create(btn_reset, "FABRIKA AYARLARINA SILA");
    lv_obj_set_style_text_color(lbl_reset, color_danger, 0);
    lv_obj_set_style_text_font(lbl_reset, &lv_font_montserrat_10, 0);
}

static void create_connection_screen(void)
{
    screen_connection = lv_obj_create(NULL);
    lv_obj_add_style(screen_connection, style_get_bg_dark(), 0);
    lv_obj_set_style_bg_color(screen_connection, color_bg_dark, 0);

    /* Header */
    lv_obj_t *header = lv_obj_create(screen_connection);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, 480, 60);
    lv_obj_set_style_bg_color(header, color_card_bg, 0);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "BAGLANTI");
    lv_obj_set_pos(title, 185, 20);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, color_primary, 0);

    /* WiFi Card */
    lv_obj_t *card_wifi = lv_obj_create(screen_connection);
    lv_obj_set_pos(card_wifi, 20, 75);
    lv_obj_set_size(card_wifi, 440, 90);
    lv_obj_set_style_bg_color(card_wifi, color_card_bg, 0);
    lv_obj_set_style_border_color(card_wifi, color_primary, 0);
    lv_obj_set_style_border_width(card_wifi, 2, 0);
    lv_obj_set_style_radius(card_wifi, 15, 0);

    lv_obj_t *wifi_icon = lv_obj_create(card_wifi);
    lv_obj_set_pos(wifi_icon, 15, 20);
    lv_obj_set_size(wifi_icon, 50, 50);
    lv_obj_set_style_bg_color(wifi_icon, color_primary, 0);
    lv_obj_set_style_radius(wifi_icon, 10, 0);

    lv_obj_t *wifi_title = lv_label_create(card_wifi);
    lv_label_set_text(wifi_title, "WiFi");
    lv_obj_set_pos(wifi_title, 80, 20);
    lv_obj_set_style_text_font(wifi_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(wifi_title, color_text, 0);

    lv_obj_t *wifi_status = lv_label_create(card_wifi);
    lv_label_set_text(wifi_status, "Connected");
    lv_obj_set_pos(wifi_status, 80, 45);
    lv_obj_set_style_text_color(wifi_status, color_success, 0);

    /* WiFi details */
    lv_obj_t *wifi_ip = lv_label_create(card_wifi);
    lv_label_set_text(wifi_ip, "IP: 192.168.1.100");
    lv_obj_set_pos(wifi_ip, 300, 20);
    lv_obj_set_style_text_color(wifi_ip, color_text_dim, 0);

    lv_obj_t *wifi_port = lv_label_create(card_wifi);
    lv_label_set_text(wifi_port, "Port: 35000");
    lv_obj_set_pos(wifi_port, 300, 40);
    lv_obj_set_style_text_color(wifi_port, color_text_dim, 0);

    /* Bluetooth Card */
    lv_obj_t *card_bt = lv_obj_create(screen_connection);
    lv_obj_set_pos(card_bt, 20, 175);
    lv_obj_set_size(card_bt, 440, 90);
    lv_obj_set_style_bg_color(card_bt, color_card_bg, 0);
    lv_obj_set_style_border_color(card_bt, color_card_border, 0);
    lv_obj_set_style_border_width(card_bt, 1, 0);
    lv_obj_set_style_radius(card_bt, 15, 0);

    lv_obj_t *bt_icon = lv_obj_create(card_bt);
    lv_obj_set_pos(bt_icon, 15, 20);
    lv_obj_set_size(bt_icon, 50, 50);
    lv_obj_set_style_bg_color(bt_icon, color_primary, 0);
    lv_obj_set_style_radius(bt_icon, 10, 0);

    lv_obj_t *bt_title = lv_label_create(card_bt);
    lv_label_set_text(bt_title, "Bluetooth");
    lv_obj_set_pos(bt_title, 80, 20);
    lv_obj_set_style_text_font(bt_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(bt_title, color_text, 0);

    lv_obj_t *bt_status = lv_label_create(card_bt);
    lv_label_set_text(bt_status, "Disconnected");
    lv_obj_set_pos(bt_status, 80, 45);
    lv_obj_set_style_text_color(bt_status, color_text_dim, 0);

    /* USB Card */
    lv_obj_t *card_usb = lv_obj_create(screen_connection);
    lv_obj_set_pos(card_usb, 20, 275);
    lv_obj_set_size(card_usb, 440, 90);
    lv_obj_set_style_bg_color(card_usb, color_card_bg, 0);
    lv_obj_set_style_border_color(card_usb, color_card_border, 0);
    lv_obj_set_style_border_width(card_usb, 1, 0);
    lv_obj_set_style_radius(card_usb, 15, 0);

    lv_obj_t *usb_icon = lv_obj_create(card_usb);
    lv_obj_set_pos(usb_icon, 15, 20);
    lv_obj_set_size(usb_icon, 50, 50);
    lv_obj_set_style_bg_color(usb_icon, color_primary, 0);
    lv_obj_set_style_radius(usb_icon, 10, 0);

    lv_obj_t *usb_title = lv_label_create(card_usb);
    lv_label_set_text(usb_title, "USB");
    lv_obj_set_pos(usb_title, 80, 20);
    lv_obj_set_style_text_font(usb_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(usb_title, color_text, 0);

    lv_obj_t *usb_status = lv_label_create(card_usb);
    lv_label_set_text(usb_status, "Disconnected");
    lv_obj_set_pos(usb_status, 80, 45);
    lv_obj_set_style_text_color(usb_status, color_text_dim, 0);

    /* Connect/Disconnect button */
    lv_obj_t *btn_connect = lv_btn_create(screen_connection);
    lv_obj_set_pos(btn_connect, 140, 380);
    lv_obj_set_size(btn_connect, 200, 45);
    lv_obj_set_style_bg_color(btn_connect, color_primary, 0);
    lv_obj_set_style_radius(btn_connect, 22, 0);

    lv_obj_t *lbl_connect = lv_label_create(btn_connect, "DISCONNECT");
    lv_obj_set_style_text_color(lbl_connect, color_text, 0);
    lv_obj_set_style_text_font(lbl_connect, &lv_font_montserrat_14, 0);
}

static void create_about_screen(void)
{
    screen_about = lv_obj_create(NULL);
    lv_obj_add_style(screen_about, style_get_bg_dark(), 0);
    lv_obj_set_style_bg_color(screen_about, color_bg_dark, 0);

    /* Header */
    lv_obj_t *header = lv_obj_create(screen_about);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, 480, 60);
    lv_obj_set_style_bg_color(header, color_card_bg, 0);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "HAKKINDA");
    lv_obj_set_pos(title, 185, 20);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, color_primary, 0);

    /* Logo */
    lv_obj_t *logo = lv_obj_create(screen_about);
    lv_obj_set_pos(logo, 200, 80);
    lv_obj_set_size(logo, 80, 80);
    lv_obj_set_style_bg_color(logo, color_primary, 0);
    lv_obj_set_style_radius(logo, 20, 0);

    /* App title */
    lv_obj_t *app_title = lv_label_create(screen_about);
    lv_label_set_text(app_title, "OBD2 RACING");
    lv_obj_set_pos(app_title, 175, 175);
    lv_obj_set_style_text_font(app_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(app_title, color_text, 0);

    /* Version */
    lv_obj_t *version = lv_label_create(screen_about);
    lv_label_set_text(version, "v1.0.0 - ESP32-S3");
    lv_obj_set_pos(version, 185, 200);
    lv_obj_set_style_text_color(version, color_primary, 0);

    /* Hardware info card */
    lv_obj_t *info_card = lv_obj_create(screen_about);
    lv_obj_set_pos(info_card, 20, 230);
    lv_obj_set_size(info_card, 440, 130);
    lv_obj_set_style_bg_color(info_card, color_card_bg, 0);
    lv_obj_set_style_border_color(info_card, color_card_border, 0);
    lv_obj_set_style_border_width(info_card, 1, 0);
    lv_obj_set_style_radius(info_card, 15, 0);

    lv_obj_t *info_title = lv_label_create(info_card);
    lv_label_set_text(info_title, "HARDWARE INFO");
    lv_obj_set_pos(info_title, 15, 10);
    lv_obj_set_style_text_font(info_title, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(info_title, color_primary, 0);

    const char *labels[] = {"Hardware", "Display", "Framework", "Protocol"};
    const char *values[] = {"ESP32-S3-Touch-LCD", "480x480 LCD", "LVGL 8.x", "ISO 15765-4"};

    for (int i = 0; i < 4; i++) {
        lv_obj_t *lbl = lv_label_create(info_card);
        lv_label_set_text(lbl, labels[i]);
        lv_obj_set_pos(lbl, 15, 35 + i * 22);
        lv_obj_set_style_text_color(lbl, color_text_dim, 0);

        lv_obj_t *val = lv_label_create(info_card);
        lv_label_set_text(val, values[i]);
        lv_obj_set_pos(val, 250, 35 + i * 22);
        lv_obj_set_style_text_color(val, color_text, 0);
    }

    /* Supported PIDs card */
    lv_obj_t *pids_card = lv_obj_create(screen_about);
    lv_obj_set_pos(pids_card, 20, 370);
    lv_obj_set_size(pids_card, 440, 100);
    lv_obj_set_style_bg_color(pids_card, color_card_bg, 0);
    lv_obj_set_style_border_color(pids_card, color_card_border, 0);
    lv_obj_set_style_border_width(pids_card, 1, 0);
    lv_obj_set_style_radius(pids_card, 15, 0);

    lv_obj_t *pids_title = lv_label_create(pids_card);
    lv_label_set_text(pids_title, "SUPPORTED PIDs");
    lv_obj_set_pos(pids_title, 15, 10);
    lv_obj_set_style_text_font(pids_title, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(pids_title, color_primary, 0);

    const char *pids[] = {
        "0x0C - Engine RPM",
        "0x0D - Vehicle Speed",
        "0x05 - Coolant Temp",
        "0x11 - Throttle Position",
        "0x2F - Fuel Level",
        "0x04 - Engine Load",
        "0x0F - Intake Air Temp",
        "0x10 - MAF Flow Rate"
    };

    for (int i = 0; i < 3; i++) {
        lv_obj_t *pid1 = lv_label_create(pids_card);
        lv_label_set_text(pid1, pids[i]);
        lv_obj_set_pos(pid1, 15, 35 + i * 20);
        lv_obj_set_style_text_color(pid1, color_text_dim, 0);

        lv_obj_t *pid2 = lv_label_create(pids_card);
        lv_label_set_text(pid2, pids[i + 3]);
        lv_obj_set_pos(pid2, 230, 35 + i * 20);
        lv_obj_set_style_text_color(pid2, color_text_dim, 0);
    }
}
