#pragma once

#include "lvgl.h"

LV_FONT_DECLARE(font_ui_12);
LV_FONT_DECLARE(font_ui_14);
LV_FONT_DECLARE(font_ui_16);
LV_FONT_DECLARE(font_ui_20);
LV_FONT_DECLARE(font_gauge_96);
LV_FONT_DECLARE(font_icons_24);
LV_FONT_DECLARE(font_icons_28);

/** Turkish-capable UI text (Montserrat subset). */
#define UI_FONT_XS      (&font_ui_12)
#define UI_FONT_SM      (&font_ui_12)
#define UI_FONT_MD      (&font_ui_14)
#define UI_FONT_LG      (&font_ui_16)
#define UI_FONT_XL      (&font_ui_20)

/** Gösterge orta değer (3x UI_FONT_LG ≈ 96 px). */
#define UI_FONT_GAUGE_VALUE  (&font_gauge_96)
#define UI_FONT_ICON    (&font_icons_24)
#define UI_FONT_ICON_LG (&font_icons_28)
