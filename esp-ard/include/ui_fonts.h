#pragma once

#include <lvgl.h>

/* Montserrat + Latin Extended (Turkish); fallback = lv_font_montserrat_* (LVGL icons) */
LV_FONT_DECLARE(lv_font_tr_10);
LV_FONT_DECLARE(lv_font_tr_12);
LV_FONT_DECLARE(lv_font_tr_14);

#define UI_FONT_XS   (&lv_font_tr_10)
#define UI_FONT_SM   (&lv_font_tr_12)
#define UI_FONT_MD   (&lv_font_tr_14)
#define UI_FONT_VALUE (&lv_font_montserrat_48)
