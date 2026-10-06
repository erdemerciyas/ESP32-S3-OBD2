#pragma once

void obd_pids_init(void);
void obd_pids_start(void);
void obd_pids_stop(void);
void obd_pids_discover(void);

/* Adaptör voltaj kalibrasyonu (ham × çarpan), seçili taşıma için; NVS'de. */
float obd_volt_cal_get(void);
void obd_volt_cal_set(float factor);
