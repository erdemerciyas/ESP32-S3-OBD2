#include "ui.h"
#include "theme.h"
#include "vehicle_data.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *s_values[9];
static lv_obj_t *s_units[9];
static char s_prev_grid_val[9][16];
static char s_prev_grid_unit[9][8];

static const char *s_names[] = {
    "TPS", "MAP", "Load",
    "IAT", "Timing", "STFT",
    "LTFT", "O2-1", "O2-2"
};

/* Static left-accent colors for each metric group */
static lv_color_t s_accents[9];

void screen_grid_create(lv_obj_t *parent)
{
    const ui_theme_t *t = theme_get();

    s_accents[0] = t->primary;   /* TPS */
    s_accents[1] = t->secondary; /* MAP */
    s_accents[2] = t->primary;   /* Load */
    s_accents[3] = t->warn;      /* IAT (temperature group) */
    s_accents[4] = t->secondary; /* Timing */
    s_accents[5] = t->ok;        /* STFT */
    s_accents[6] = t->ok;        /* LTFT */
    s_accents[7] = t->secondary; /* O2-1 */
    s_accents[8] = t->secondary; /* O2-2 */

    lv_obj_t *body = theme_create_flex_col(parent, true);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_style_pad_row(body, UI_GAP_MD, 0);

    theme_create_header(body, "Live Data");

    lv_obj_t *grid = theme_create_flex_col(body, true);
    lv_obj_set_style_pad_row(grid, UI_GAP, 0);

    int idx = 0;
    for (int r = 0; r < 3; r++) {
        lv_obj_t *row = theme_create_flex_row(grid);
        lv_obj_set_flex_grow(row, 1);

        for (int c = 0; c < 3; c++) {
            theme_create_metric_cell(row, s_names[idx], s_accents[idx],
                                     &s_values[idx], &s_units[idx]);
            idx++;
        }
    }
}

void screen_grid_update(const vehicle_data_snapshot_t *snap)
{
    char val_buf[16];
    char unit_buf[8];

    /* 0: TPS */
    snprintf(val_buf, sizeof(val_buf), "%.0f", snap->throttle);
    strncpy(unit_buf, "%", sizeof(unit_buf));
    if (strcmp(val_buf, s_prev_grid_val[0]) != 0) {
        strncpy(s_prev_grid_val[0], val_buf, sizeof(s_prev_grid_val[0]));
        lv_label_set_text(s_values[0], val_buf);
    }
    if (strcmp(unit_buf, s_prev_grid_unit[0]) != 0) {
        strncpy(s_prev_grid_unit[0], unit_buf, sizeof(s_prev_grid_unit[0]));
        lv_label_set_text(s_units[0], unit_buf);
    }

    /* 1: MAP */
    snprintf(val_buf, sizeof(val_buf), "%.0f", snap->map);
    strncpy(unit_buf, "kPa", sizeof(unit_buf));
    if (strcmp(val_buf, s_prev_grid_val[1]) != 0) {
        strncpy(s_prev_grid_val[1], val_buf, sizeof(s_prev_grid_val[1]));
        lv_label_set_text(s_values[1], val_buf);
    }
    if (strcmp(unit_buf, s_prev_grid_unit[1]) != 0) {
        strncpy(s_prev_grid_unit[1], unit_buf, sizeof(s_prev_grid_unit[1]));
        lv_label_set_text(s_units[1], unit_buf);
    }

    /* 2: Load */
    snprintf(val_buf, sizeof(val_buf), "%.0f", snap->load);
    strncpy(unit_buf, "%", sizeof(unit_buf));
    if (strcmp(val_buf, s_prev_grid_val[2]) != 0) {
        strncpy(s_prev_grid_val[2], val_buf, sizeof(s_prev_grid_val[2]));
        lv_label_set_text(s_values[2], val_buf);
    }
    if (strcmp(unit_buf, s_prev_grid_unit[2]) != 0) {
        strncpy(s_prev_grid_unit[2], unit_buf, sizeof(s_prev_grid_unit[2]));
        lv_label_set_text(s_units[2], unit_buf);
    }

    /* 3: IAT */
    snprintf(val_buf, sizeof(val_buf), "%.0f",
             vehicle_data_convert_temp(snap->iat, snap->metric_units));
    strncpy(unit_buf, vehicle_data_temp_unit(snap->metric_units), sizeof(unit_buf));
    if (strcmp(val_buf, s_prev_grid_val[3]) != 0) {
        strncpy(s_prev_grid_val[3], val_buf, sizeof(s_prev_grid_val[3]));
        lv_label_set_text(s_values[3], val_buf);
    }
    if (strcmp(unit_buf, s_prev_grid_unit[3]) != 0) {
        strncpy(s_prev_grid_unit[3], unit_buf, sizeof(s_prev_grid_unit[3]));
        lv_label_set_text(s_units[3], unit_buf);
    }

    /* 4: Timing */
    snprintf(val_buf, sizeof(val_buf), "%.1f", snap->timing);
    strncpy(unit_buf, "°", sizeof(unit_buf));
    if (strcmp(val_buf, s_prev_grid_val[4]) != 0) {
        strncpy(s_prev_grid_val[4], val_buf, sizeof(s_prev_grid_val[4]));
        lv_label_set_text(s_values[4], val_buf);
    }
    if (strcmp(unit_buf, s_prev_grid_unit[4]) != 0) {
        strncpy(s_prev_grid_unit[4], unit_buf, sizeof(s_prev_grid_unit[4]));
        lv_label_set_text(s_units[4], unit_buf);
    }

    /* 5: STFT */
    snprintf(val_buf, sizeof(val_buf), "%+.1f", snap->fuel_trim_st);
    strncpy(unit_buf, "%", sizeof(unit_buf));
    if (strcmp(val_buf, s_prev_grid_val[5]) != 0) {
        strncpy(s_prev_grid_val[5], val_buf, sizeof(s_prev_grid_val[5]));
        lv_label_set_text(s_values[5], val_buf);
    }
    if (strcmp(unit_buf, s_prev_grid_unit[5]) != 0) {
        strncpy(s_prev_grid_unit[5], unit_buf, sizeof(s_prev_grid_unit[5]));
        lv_label_set_text(s_units[5], unit_buf);
    }

    /* 6: LTFT */
    snprintf(val_buf, sizeof(val_buf), "%+.1f", snap->fuel_trim_lt);
    strncpy(unit_buf, "%", sizeof(unit_buf));
    if (strcmp(val_buf, s_prev_grid_val[6]) != 0) {
        strncpy(s_prev_grid_val[6], val_buf, sizeof(s_prev_grid_val[6]));
        lv_label_set_text(s_values[6], val_buf);
    }
    if (strcmp(unit_buf, s_prev_grid_unit[6]) != 0) {
        strncpy(s_prev_grid_unit[6], unit_buf, sizeof(s_prev_grid_unit[6]));
        lv_label_set_text(s_units[6], unit_buf);
    }

    /* 7: O2-1 */
    snprintf(val_buf, sizeof(val_buf), "%.2f", snap->o2_voltage);
    strncpy(unit_buf, "V", sizeof(unit_buf));
    if (strcmp(val_buf, s_prev_grid_val[7]) != 0) {
        strncpy(s_prev_grid_val[7], val_buf, sizeof(s_prev_grid_val[7]));
        lv_label_set_text(s_values[7], val_buf);
    }
    if (strcmp(unit_buf, s_prev_grid_unit[7]) != 0) {
        strncpy(s_prev_grid_unit[7], unit_buf, sizeof(s_prev_grid_unit[7]));
        lv_label_set_text(s_units[7], unit_buf);
    }

    /* 8: O2-2 */
    snprintf(val_buf, sizeof(val_buf), "%.2f", snap->o2_b1s2);
    strncpy(unit_buf, "V", sizeof(unit_buf));
    if (strcmp(val_buf, s_prev_grid_val[8]) != 0) {
        strncpy(s_prev_grid_val[8], val_buf, sizeof(s_prev_grid_val[8]));
        lv_label_set_text(s_values[8], val_buf);
    }
    if (strcmp(unit_buf, s_prev_grid_unit[8]) != 0) {
        strncpy(s_prev_grid_unit[8], unit_buf, sizeof(s_prev_grid_unit[8]));
        lv_label_set_text(s_units[8], unit_buf);
    }
}
