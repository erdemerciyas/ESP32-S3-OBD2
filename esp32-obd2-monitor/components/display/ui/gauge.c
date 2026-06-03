#include "gauge.h"
#include "styles.h"
#include "app.h"
#include "esp_log.h"

static const char *TAG = "gauge";

typedef struct {
    lv_obj_t *arc_bg;
    lv_obj_t *arc_indicator;
    lv_obj_t *label_value;
    lv_obj_t *label_unit;
    lv_obj_t *label_name;
    lv_obj_t *card_bg;

    int16_t min_value;
    int16_t max_value;
    int16_t current_value;

    lv_color_t color_normal;
    lv_color_t color_warning;
    lv_color_t color_danger;
} gauge_widget_t;

/* Gauge definitions matching ui-demo.html - 2005 Chevrolet Kalos */
typedef struct {
    const char *label;
    const char *unit;
    int16_t max_value;
    lv_color_t color;
} gauge_config_t;

static const gauge_config_t gauge_configs[GAUGE_MAX] = {
    {"RPM", "RPM", 6500, LV_COLOR_MAKE(0xFF, 0x2D, 0x2D)},      // Red
    {"SPEED", "km/h", 180, LV_COLOR_MAKE(0xFF, 0x6B, 0x35)},    // Orange
    {"COOLANT", "°C", 120, LV_COLOR_MAKE(0xFF, 0x44, 0x44)},    // Red
    {"BATTERY", "V", 16, LV_COLOR_MAKE(0x00, 0xFF, 0x88)},      // Green
    {"THROTTLE", "%", 100, LV_COLOR_MAKE(0xFF, 0xCC, 0x00)},    // Yellow
    {"FUEL", "%", 100, LV_COLOR_MAKE(0x00, 0xFF, 0x88)},        // Green
    {"LOAD", "%", 100, LV_COLOR_MAKE(0xFF, 0x2D, 0x2D)},        // Red
    {"INTAKE", "°C", 80, LV_COLOR_MAKE(0xFF, 0x88, 0x00)},      // Orange
    {"FUEL RATE", "L/h", 25, LV_COLOR_MAKE(0x00, 0xCC, 0xFF)},  // Cyan
    {"DTC", "CODES", 10, LV_COLOR_MAKE(0xFF, 0x00, 0x00)}       // Red for warning
};

static gauge_widget_t gauges[GAUGE_MAX];

/* Full-screen gauge objects */
static lv_obj_t *fullscreen_gauge_container;
static lv_obj_t *fullscreen_arc_bg;
static lv_obj_t *fullscreen_arc_indicator;
static lv_obj_t *fullscreen_label_name;
static lv_obj_t *fullscreen_label_value;
static lv_obj_t *fullscreen_label_unit;
static lv_obj_t *indicator_dots[GAUGE_MAX];
static gauge_type_t current_fullscreen_gauge = GAUGE_RPM;
static int16_t fullscreen_gauge_values[GAUGE_MAX] = {0};

void gauge_create_widget(lv_obj_t *parent, gauge_type_t type, int16_t x, int16_t y)
{
    gauge_widget_t *g = &gauges[type];

    g->min_value = 0;
    g->max_value = 100;
    g->current_value = 0;

    /* Card background */
    g->card_bg = lv_obj_create(parent);
    lv_obj_set_pos(g->card_bg, x, y);
    lv_obj_set_size(g->card_bg, 140, 140);
    lv_obj_set_style_bg_color(g->card_bg, color_card_bg, 0);
    lv_obj_set_style_border_color(g->card_bg, color_card_border, 0);
    lv_obj_set_style_border_width(g->card_bg, 1, 0);
    lv_obj_set_style_radius(g->card_bg, 15, 0);

    /* Top gradient line */
    lv_obj_t *top_line = lv_obj_create(g->card_bg);
    lv_obj_set_size(top_line, 140, 3);
    lv_obj_set_pos(top_line, 0, 0);
    lv_obj_set_style_bg_color(top_line, color_primary, 0);
    lv_obj_set_style_radius(top_line, 0, 0);

    /* Arc background track */
    g->arc_bg = lv_arc_create(g->card_bg);
    lv_obj_set_pos(g->arc_bg, 20, 15);
    lv_obj_set_size(g->arc_bg, 100, 100);
    lv_arc_set_bg_angles(g->arc_bg, 135, 405);
    lv_arc_set_rotation(g->arc_bg, 0);
    lv_arc_set_range(g->arc_bg, 0, 270);
    lv_obj_set_style_arc_color(g->arc_bg, color_card_border, LV_PART_MAIN);
    lv_obj_set_style_arc_width(g->arc_bg, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_color(g->arc_bg, color_card_border, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(g->arc_bg, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(g->arc_bg, true, LV_PART_INDICATOR);

    /* Value label - centered in gauge */
    g->label_value = lv_label_create(g->card_bg);
    lv_label_set_text_fmt(g->label_value, "%d", 0);
    lv_obj_set_pos(g->label_value, 55, 40);
    lv_obj_set_style_text_font(g->label_value, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(g->label_value, color_text, 0);

    /* Unit label */
    g->label_unit = lv_label_create(g->card_bg);
    lv_obj_set_pos(g->label_unit, 55, 70);
    lv_obj_set_style_text_font(g->label_unit, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(g->label_unit, color_text_dim, 0);

    /* Name label at bottom */
    g->label_name = lv_label_create(g->card_bg);
    lv_obj_set_pos(g->label_name, 45, 115);
    lv_obj_set_style_text_font(g->label_name, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(g->label_name, color_accent, 0);

    switch (type) {
        case GAUGE_SPEED:
            g->max_value = 260;
            g->color_normal = color_secondary;
            g->color_warning = color_warning;
            g->color_danger = color_danger;
            lv_label_set_text(g->label_unit, "km/h");
            lv_label_set_text(g->label_name, "SPEED");
            break;
        case GAUGE_RPM:
            g->max_value = 8000;
            g->color_normal = color_primary;
            g->color_warning = color_warning;
            g->color_danger = color_danger;
            lv_label_set_text(g->label_unit, "RPM");
            lv_label_set_text(g->label_name, "RPM");
            break;
        case GAUGE_COOLANT:
            g->max_value = 130;
            g->color_normal = color_success;
            g->color_warning = color_warning;
            g->color_danger = color_danger;
            lv_label_set_text(g->label_unit, "\u00B0C");
            lv_label_set_text(g->label_name, "COOLANT");
            break;
        case GAUGE_THROTTLE:
            g->max_value = 100;
            g->color_normal = color_accent;
            g->color_warning = color_warning;
            g->color_danger = color_danger;
            lv_label_set_text(g->label_unit, "%");
            lv_label_set_text(g->label_name, "THROTTLE");
            break;
        case GAUGE_FUEL:
            g->max_value = 100;
            g->color_normal = color_success;
            g->color_warning = color_warning;
            g->color_danger = color_danger;
            lv_label_set_text(g->label_unit, "%");
            lv_label_set_text(g->label_name, "FUEL");
            break;
        case GAUGE_LOAD:
            g->max_value = 100;
            g->color_normal = color_primary;
            g->color_warning = color_warning;
            g->color_danger = color_danger;
            lv_label_set_text(g->label_unit, "%");
            lv_label_set_text(g->label_name, "LOAD");
            break;
        default:
            break;
    }
}

void gauge_set_value(gauge_type_t type, int16_t value)
{
    if (type >= GAUGE_MAX) return;

    gauge_widget_t *g = &gauges[type];
    g->current_value = value;

    int16_t clamped = value;
    if (clamped < g->min_value) clamped = g->min_value;
    if (clamped > g->max_value) clamped = g->max_value;

    /* Calculate angle: 0-270 degrees mapped to value range */
    uint16_t angle = (uint16_t)((float)(clamped - g->min_value) / (g->max_value - g->min_value) * 270);
    lv_arc_set_value(g->arc_bg, angle);

    lv_label_set_text_fmt(g->label_value, "%d", value);

    /* Dynamic color based on value ratio */
    float ratio = (float)clamped / g->max_value;
    lv_color_t arc_color;
    if (ratio < 0.6f) {
        arc_color = g->color_normal;
    } else if (ratio < 0.85f) {
        arc_color = g->color_warning;
    } else {
        arc_color = g->color_danger;
    }
    lv_obj_set_style_arc_color(g->arc_bg, arc_color, LV_PART_INDICATOR);
}

int16_t gauge_get_value(gauge_type_t type)
{
    if (type >= GAUGE_MAX) return 0;
    return gauges[type].current_value;
}

/* Full-screen gauge implementation - matching ui-demo.html */
void gauge_create_fullscreen(lv_obj_t *parent, gauge_type_t type)
{
    ESP_LOGI(TAG, "Creating fullscreen gauge...");
    
    /* Create container for fullscreen gauge */
    fullscreen_gauge_container = lv_obj_create(parent);
    lv_obj_set_size(fullscreen_gauge_container, 460, 460);
    lv_obj_set_pos(fullscreen_gauge_container, 10, 110);
    lv_obj_set_style_bg_color(fullscreen_gauge_container, color_bg_dark, 0);
    lv_obj_set_style_border_width(fullscreen_gauge_container, 0, 0);
    
    /* Create SVG-style arc gauge (270 degree arc) */
    /* Background arc */
    fullscreen_arc_bg = lv_arc_create(fullscreen_gauge_container);
    lv_obj_set_size(fullscreen_arc_bg, 440, 440);
    lv_obj_center(fullscreen_arc_bg);
    lv_arc_set_bg_angles(fullscreen_arc_bg, 135, 405);  /* 270 degree arc */
    lv_arc_set_range(fullscreen_arc_bg, 0, 1000);  /* 0-1000 for precision */
    lv_obj_set_style_arc_color(fullscreen_arc_bg, color_card_border, LV_PART_MAIN);
    lv_obj_set_style_arc_width(fullscreen_arc_bg, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(fullscreen_arc_bg, true, LV_PART_MAIN);
    
    /* Remove knob */
    lv_obj_remove_style(fullscreen_arc_bg, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_color(fullscreen_arc_bg, color_primary, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(fullscreen_arc_bg, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(fullscreen_arc_bg, true, LV_PART_INDICATOR);
    fullscreen_arc_indicator = fullscreen_arc_bg;
    
    /* Center label - Name */
    fullscreen_label_name = lv_label_create(fullscreen_gauge_container);
    lv_label_set_text(fullscreen_label_name, gauge_configs[type].label);
    lv_obj_align(fullscreen_label_name, LV_ALIGN_CENTER, 0, -70);
    lv_obj_set_style_text_font(fullscreen_label_name, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(fullscreen_label_name, color_accent, 0);
    lv_obj_set_style_text_letter_space(fullscreen_label_name, 4, 0);
    
    /* Center label - Value */
    fullscreen_label_value = lv_label_create(fullscreen_gauge_container);
    lv_label_set_text_fmt(fullscreen_label_value, "%d", 0);
    lv_obj_align(fullscreen_label_value, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_style_text_font(fullscreen_label_value, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(fullscreen_label_value, color_text, 0);
    lv_obj_set_style_text_line_space(fullscreen_label_value, 0, 0);
    
    /* Center label - Unit */
    fullscreen_label_unit = lv_label_create(fullscreen_gauge_container);
    lv_label_set_text(fullscreen_label_unit, gauge_configs[type].unit);
    lv_obj_align(fullscreen_label_unit, LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_style_text_font(fullscreen_label_unit, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(fullscreen_label_unit, color_text_dim, 0);
    
    /* Create indicator dots at bottom */
    int dot_start_x = 135;
    int dot_spacing = 30;
    for (int i = 0; i < GAUGE_MAX; i++) {
        indicator_dots[i] = lv_obj_create(fullscreen_gauge_container);
        lv_obj_set_size(indicator_dots[i], 10, 10);
        lv_obj_set_pos(indicator_dots[i], dot_start_x + (i * dot_spacing), 420);
        lv_obj_set_style_bg_color(indicator_dots[i], (i == 0) ? color_primary : color_card_border, 0);
        lv_obj_set_style_radius(indicator_dots[i], 5, 0);
        lv_obj_set_style_border_width(indicator_dots[i], 0, 0);
    }
    
    /* Initialize with first gauge */
    gauge_set_active(type);
    
    ESP_LOGI(TAG, "Fullscreen gauge created");
}

void gauge_update_fullscreen(gauge_type_t type, int16_t value)
{
    if (type >= GAUGE_MAX) return;
    
    /* Store value */
    fullscreen_gauge_values[type] = value;
    
    /* If this is the active gauge, update display */
    if (type == current_fullscreen_gauge) {
        const gauge_config_t *config = &gauge_configs[type];
        
        /* Clamp value */
        int16_t clamped = value;
        if (clamped < 0) clamped = 0;
        if (clamped > config->max_value) clamped = config->max_value;
        
        /* Calculate arc value (0-1000 range for 270 degree arc) */
        uint16_t arc_value = (uint16_t)((float)clamped / config->max_value * 1000);
        lv_arc_set_value(fullscreen_arc_indicator, arc_value);
        
        /* Update value label */
        lv_label_set_text_fmt(fullscreen_label_value, "%d", value);
        
        /* Dynamic color based on value ratio */
        float ratio = (float)clamped / config->max_value;
        lv_color_t arc_color;
        if (ratio < 0.6f) {
            arc_color = config->color;
        } else if (ratio < 0.85f) {
            arc_color = color_warning;
        } else {
            arc_color = color_danger;
        }
        lv_obj_set_style_arc_color(fullscreen_arc_indicator, arc_color, LV_PART_INDICATOR);
        
        /* Update value color with glow effect */
        if (ratio >= 0.85f) {
            lv_obj_set_style_text_color(fullscreen_label_value, color_danger, 0);
        } else {
            lv_obj_set_style_text_color(fullscreen_label_value, color_text, 0);
        }
    }
}

void gauge_set_active(gauge_type_t type)
{
    if (type >= GAUGE_MAX) return;
    
    current_fullscreen_gauge = type;
    const gauge_config_t *config = &gauge_configs[type];
    
    /* Update gauge label */
    lv_label_set_text(fullscreen_label_name, config->label);
    lv_obj_set_style_text_color(fullscreen_label_name, config->color, 0);
    
    /* Update unit */
    lv_label_set_text(fullscreen_label_unit, config->unit);
    
    /* Update arc color to gauge color */
    lv_obj_set_style_arc_color(fullscreen_arc_indicator, config->color, LV_PART_INDICATOR);
    
    /* Update current value display */
    int16_t value = fullscreen_gauge_values[type];
    int16_t clamped = value;
    if (clamped < 0) clamped = 0;
    if (clamped > config->max_value) clamped = config->max_value;
    
    uint16_t arc_value = (uint16_t)((float)clamped / config->max_value * 1000);
    lv_arc_set_value(fullscreen_arc_indicator, arc_value);
    lv_label_set_text_fmt(fullscreen_label_value, "%d", value);
    
    /* Update indicator dots */
    for (int i = 0; i < GAUGE_MAX; i++) {
        lv_obj_set_style_bg_color(indicator_dots[i], (i == type) ? color_primary : color_card_border, 0);
        if (i == type) {
            /* Add glow effect to active dot */
            lv_obj_set_style_shadow_width(indicator_dots[i], 12, 0);
            lv_obj_set_style_shadow_color(indicator_dots[i], color_primary, 0);
        } else {
            lv_obj_set_style_shadow_width(indicator_dots[i], 0, 0);
        }
    }
}

gauge_type_t gauge_get_active(void)
{
    return current_fullscreen_gauge;
}
