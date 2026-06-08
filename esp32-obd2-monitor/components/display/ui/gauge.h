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
void gauge_set_value_valid(gauge_type_t type, bool valid);
void gauge_create_indicator_row(lv_obj_t *parent);
void gauge_update_indicator_row(gauge_type_t active);
void gauge_raise_indicator_layers(void);
void gauge_tick(void);
void gauge_set_active(gauge_type_t type);
gauge_type_t gauge_get_active(void);
void gauge_sync_availability(void);
bool gauge_is_available(gauge_type_t type);
gauge_type_t gauge_next_available(gauge_type_t from);
gauge_type_t gauge_prev_available(gauge_type_t from);
gauge_type_t gauge_first_available(void);
bool gauge_is_transitioning(void);
lv_obj_t *gauge_get_container(void);
const char *gauge_get_label(gauge_type_t type);
uint32_t gauge_get_color(gauge_type_t type);
void gauge_settings_changed(void);
void gauge_swap_order_slots(int slot_a, int slot_b);

#ifdef __cplusplus
}
#endif
