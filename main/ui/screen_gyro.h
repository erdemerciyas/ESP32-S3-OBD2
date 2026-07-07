#pragma once

#include "lvgl.h"
#include "imu_data.h"

/**
 * @brief  Build the gyro off-road screen inside the given tab page.
 *         Called once during ui_init().
 */
void screen_gyro_create(lv_obj_t *parent);

/**
 * @brief  Update the gyro screen with the latest IMU snapshot.
 *         Called at 60 Hz from the LVGL update timer.
 */
void screen_gyro_update(const imu_snapshot_t *snap);
