#include "ui.h"
#include "theme.h"
#include "vehicle_data.h"
#include <stdio.h>

static lv_obj_t *s_values[9];

static const char *s_names[] = {
    "TPS", "MAP", "Load",
    "IAT", "Timing", "STFT",
    "LTFT", "O2-1", "O2-2"
};

void screen_grid_create(lv_obj_t *parent)
{
    theme_create_header(parent, "Live Data");

    lv_obj_t *grid = theme_create_flex_col(parent, true);

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

    snprintf(buf, sizeof(buf), "%.0f%%", vd->throttle);
    lv_label_set_text(s_values[0], buf);

    snprintf(buf, sizeof(buf), "%.0fkPa", vd->map);
    lv_label_set_text(s_values[1], buf);

    snprintf(buf, sizeof(buf), "%.0f%%", vd->load);
    lv_label_set_text(s_values[2], buf);

    snprintf(buf, sizeof(buf), "%.0f%s",
             vehicle_data_convert_temp(vd->iat, vd->metric_units),
             vehicle_data_temp_unit(vd->metric_units));
    lv_label_set_text(s_values[3], buf);

    snprintf(buf, sizeof(buf), "%.1f", vd->timing);
    lv_label_set_text(s_values[4], buf);

    snprintf(buf, sizeof(buf), "%+.1f%%", vd->fuel_trim_st);
    lv_label_set_text(s_values[5], buf);

    snprintf(buf, sizeof(buf), "%+.1f%%", vd->fuel_trim_lt);
    lv_label_set_text(s_values[6], buf);

    snprintf(buf, sizeof(buf), "%.2fV", vd->o2_voltage);
    lv_label_set_text(s_values[7], buf);

    snprintf(buf, sizeof(buf), "%.2fV", vd->o2_b1s2);
    lv_label_set_text(s_values[8], buf);
}
