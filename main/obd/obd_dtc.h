#pragma once

#include <stdbool.h>

void obd_dtc_init(void);
void obd_dtc_read_all(void);
void obd_dtc_read_active(void);
void obd_dtc_read_pending(void);
void obd_dtc_clear(void);
bool obd_dtc_is_busy(void);
void obd_dtc_reset(void);
