#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
#define LV_HOR_RES_MAX 480
#define LV_VER_RES_MAX 480
#define LV_DPI_DEF 120

#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC   malloc
#define LV_MEM_CUSTOM_FREE    free
#define LV_MEM_CUSTOM_REALLOC realloc

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_36 1
#define LV_FONT_MONTSERRAT_48 1

#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_BTN 1
#define LV_USE_LABEL 1
#define LV_USE_LIST 1
#define LV_USE_IMG 1
#define LV_USE_LINE 1
#define LV_USE_TABLE 1
#define LV_USE_METER 1
#define LV_USE_LED 1

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

#define LV_DRAW_COMPLEX 1
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

#endif
