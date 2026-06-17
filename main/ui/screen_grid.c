#include "ui.h"
#include "theme.h"
#include "vehicle_data.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *s_values[9];
static char s_prev_grid[9][24];

static const char *s_names[] = {
    "TPS", "MAP", "Load",
    "IAT", "Timing", "STFT",
    "LTFT", "O2-1", "O2-2"
};

void screen_grid_create(lv_obj_t *parent)
{
    const lv_coord_t y_top = UI_PAD_TOP + 34;
    const lv_coord_t y_bottom = UI_VIEWPORT_SZ - UI_STAT_BOTTOM_OFF;
    const lv_coord_t safe_w = theme_safe_width(y_top, y_bottom);

    lv_obj_t *wrap = lv_obj_create(parent);
    lv_obj_set_width(wrap, safe_w);
    lv_obj_set_height(wrap, y_bottom - y_top);
    lv_obj_align(wrap, LV_ALIGN_TOP_MID, 0, y_top);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wrap, 0, 0);
    lv_obj_set_style_pad_all(wrap, 0, 0);
    lv_obj_set_style_pad_row(wrap, UI_GAP, 0);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);

    theme_create_header(wrap, "Live Data");

    lv_obj_t *grid = theme_create_flex_col(wrap, true);

    int idx = 0;
    for (int r = 0; r < 3; r++) {
        lv_obj_t *row = theme_create_flex_row(grid);
        lv_obj_set_flex_grow(row, 1);

        for (int c = 0; c < 3; c++) {
            theme_create_metric_cell(row, s_names[idx], &s_values[idx]);
            idx++;
        }
    }
}

void screen_grid_update(void)
{
    vehicle_data_t *vd = vehicle_data_get();
    char buf[24];

#define GRID_SET(idx, fmt, ...)                          \
    do {                                                  \
        snprintf(buf, sizeof(buf), fmt, ##__VA_ARGS__);  \
        if (strcmp(buf, s_prev_grid[idx]) != 0) {         \
            strncpy(s_prev_grid[idx], buf, sizeof(s_prev_grid[idx])); \
            lv_label_set_text(s_values[idx], buf);        \
        }                                                 \
    } while (0)

    GRID_SET(0, "%.0f%%", vd->throttle);
    GRID_SET(1, "%.0fkPa", vd->map);
    GRID_SET(2, "%.0f%%", vd->load);
    GRID_SET(3, "%.0f%s",
             vehicle_data_convert_temp(vd->iat, vd->metric_units),
             vehicle_data_temp_unit(vd->metric_units));
    GRID_SET(4, "%.1f", vd->timing);
    GRID_SET(5, "%+.1f%%", vd->fuel_trim_st);
    GRID_SET(6, "%+.1f%%", vd->fuel_trim_lt);
    GRID_SET(7, "%.2fV", vd->o2_voltage);
    GRID_SET(8, "%.2fV", vd->o2_b1s2);

#undef GRID_SET
}
