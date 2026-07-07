#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Snapshot of fused IMU orientation — read by the UI each frame. */
typedef struct {
    float pitch_deg;        /* Forward/back tilt (positive = nose up)    */
    float roll_deg;         /* Left/right tilt (positive = right down)   */
    float yaw_deg;          /* Heading 0-360 (gyro integration, drifts)  */
    float pitch_rad;
    float roll_rad;
    float yaw_rad;
    float accel_x;          /* Raw accel m/s² (for diagnostics)          */
    float accel_y;
    float accel_z;
    float gyro_x;           /* Raw gyro rad/s (for diagnostics)          */
    float gyro_y;
    float gyro_z;
    float temperature;      /* °C                                        */
    uint32_t sample_ts;     /* lv_tick_get() when last sample arrived    */
    bool    fresh;          /* true if data has been updated recently    */
} imu_snapshot_t;

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

/**
 * @brief  Initialise the QMI8658 driver, run gyro calibration, and create
 *         the 100 Hz polling task. Blocks ~3 s during calibration.
 */
void imu_init(void);

/**
 * @brief  Start (or resume) the IMU polling task.
 */
void imu_start(void);

/**
 * @brief  Stop the IMU polling task (e.g. to save power).
 */
void imu_stop(void);

/**
 * @brief  Get a consistent snapshot of the latest fused orientation.
 *         Thread-safe — may be called from the LVGL timer ISR context.
 */
void imu_get_snapshot(imu_snapshot_t *snap);

/**
 * @brief  Trigger a gyro calibration cycle (vehicle must be stationary).
 *         Blocks the caller for ~2 s.
 */
void imu_calibrate(void);

/**
 * @brief  Return true if the IMU has delivered fresh data within the last
 *         500 ms.
 */
bool imu_is_fresh(void);
