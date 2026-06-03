#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GAUGE_RPM = 0,
    GAUGE_SPEED,
    GAUGE_COOLANT,
    GAUGE_VOLTAGE,
    GAUGE_THROTTLE,
    GAUGE_FUEL,
    GAUGE_LOAD,
    GAUGE_INTAKE,
    GAUGE_FUEL_CONSUMPTION,
    GAUGE_DTC_WARNING,
    GAUGE_MAX
} gauge_type_t;

void gauge_create_widget(lv_obj_t *parent, gauge_type_t type, int16_t x, int16_t y);
void gauge_set_value(gauge_type_t type, int16_t value);
int16_t gauge_get_value(gauge_type_t type);

/* Full-screen gauge functions */
void gauge_create_fullscreen(lv_obj_t *parent, gauge_type_t type);
void gauge_update_fullscreen(gauge_type_t type, int16_t value);
void gauge_tick(void);
void gauge_set_active(gauge_type_t type);
gauge_type_t gauge_get_active(void);
bool gauge_is_transitioning(void);
lv_obj_t *gauge_get_container(void);

#ifdef __cplusplus
}
#endif
