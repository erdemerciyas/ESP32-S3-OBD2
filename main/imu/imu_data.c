#include "imu_data.h"
#include "qmi8658.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static const char *TAG = "imu";

/* Internal fused-data state (mutex-protected). */
typedef struct {
    float pitch_rad;
    float roll_rad;
    float yaw_rad;
    float accel_x, accel_y, accel_z;
    float gyro_x, gyro_y, gyro_z;
    float temperature;
    uint32_t sample_ts;
    bool    fresh;
    bool    init_done;       /* first accel reading used to seed angles */
} imu_data_t;

static imu_data_t      s_imu;
static SemaphoreHandle_t s_mutex;
static TaskHandle_t     s_task;
static volatile bool    s_task_running;

/* Complementary filter coefficient — fraction of gyro integration per sample.
 * At 100 Hz with τ ≈ 0.5 s: alpha = τ/(τ+dt) ≈ 0.98 */
#define IMU_FILTER_ALPHA   0.98f

/* Stale timeout — values older than this are "not fresh". */
#define IMU_STALE_MS       500

/* ---------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

extern uint32_t lv_tick_get(void);

static void imu_lock(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
}

static void imu_unlock(void)
{
    xSemaphoreGive(s_mutex);
}

/* Compute pitch and roll from the accelerometer gravity vector.
 * Assumes the IMU is mounted with Z-up, X-forward, Y-right.
 * Returns pitch_rad, roll_rad via pointers. */
static void accel_to_angles(float ax, float ay, float az,
                            float *pitch, float *roll)
{
    /* Pitch: angle between the X-Z projection and the horizontal plane.
     * Roll:  angle between the Y-Z projection and the horizontal plane.
     *
     * Standard aerospace convention (arbitrary but consistent):
     *   pitch = atan2( ax, sqrt(ay*ay + az*az) )
     *   roll  = atan2( ay, sqrt(ax*ax + az*az) )
     *
     * For automotive off-road we swap X/Y so that:
     *   pitch = forward/back tilt → accelerometer X axis
     *   roll  = side tilt         → accelerometer Y axis
     */
    float ax_norm = ax;
    float ay_norm = ay;
    float az_norm = az;

    /* Avoid division by zero / NaN. */
    if (fabsf(az) < 0.01f) {
        az_norm = (az >= 0.0f) ? 0.01f : -0.01f;
    }

    *pitch = atan2f(-ax_norm, sqrtf(ay_norm * ay_norm + az_norm * az_norm));
    *roll  = atan2f( ay_norm, az_norm);
}

/* ---------------------------------------------------------------------------
 * Polling task (100 Hz)
 * ------------------------------------------------------------------------- */

static void imu_task(void *arg)
{
    (void)arg;
    uint32_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(10);   /* 100 Hz */

    /* Local filter state */
    float pitch_rad = 0.0f;
    float roll_rad  = 0.0f;
    float yaw_rad   = 0.0f;
    uint32_t last_sample_us = 0;

    while (s_task_running) {
        qmi8658_data_t raw;
        if (qmi8658_read_sensors(&raw)) {
            uint32_t now = lv_tick_get();

            /* Time delta for gyro integration (seconds). */
            float dt = (last_sample_us > 0)
                           ? (float)(now - last_sample_us) / 1000.0f
                           : 0.01f;   /* assume 10 ms on first sample */
            if (dt <= 0.0f)  { dt = 0.001f; }
            if (dt > 0.1f)   { dt = 0.1f; }   /* cap at 100 ms (lost frames) */
            last_sample_us = now;

            /* Accelerometer-derived angles */
            float accel_pitch, accel_roll;
            accel_to_angles(raw.accel_x, raw.accel_y, raw.accel_z,
                            &accel_pitch, &accel_roll);

            /* Complementary filter: blend gyro integration with accel reference.
             *   angle = alpha * (angle + gyro*dt) + (1-alpha) * accel_angle
             * Gyro axes map to same physical rotation as accel axes. */
            if (!s_imu.init_done) {
                /* Seed with the accelerometer on first valid reading. */
                pitch_rad = accel_pitch;
                roll_rad  = accel_roll;
                yaw_rad   = 0.0f;
                s_imu.init_done = true;
            } else {
                /* Gyro rates are in rad/s around each axis.
                 * QMI8658 gyro X = roll rate, Y = pitch rate, Z = yaw rate
                 * (depends on physical mounting — adjust signs if needed). */
                float gyro_pitch_rate = raw.gyro_y;   /* Y gyro → pitch */
                float gyro_roll_rate  = raw.gyro_x;   /* X gyro → roll  */
                float gyro_yaw_rate   = raw.gyro_z;   /* Z gyro → yaw   */

                float pitch_from_gyro = pitch_rad + gyro_pitch_rate * dt;
                float roll_from_gyro  = roll_rad  + gyro_roll_rate  * dt;
                float yaw_from_gyro   = yaw_rad   + gyro_yaw_rate   * dt;

                pitch_rad = IMU_FILTER_ALPHA * pitch_from_gyro
                          + (1.0f - IMU_FILTER_ALPHA) * accel_pitch;
                roll_rad  = IMU_FILTER_ALPHA * roll_from_gyro
                          + (1.0f - IMU_FILTER_ALPHA) * accel_roll;
                yaw_rad   = yaw_from_gyro;   /* no absolute yaw reference */
            }

            /* Keep yaw in [0, 2π). */
            while (yaw_rad < 0.0f)       { yaw_rad += 2.0f * M_PI; }
            while (yaw_rad >= 2.0f * M_PI) { yaw_rad -= 2.0f * M_PI; }

            /* Store fused state. */
            imu_lock();
            s_imu.pitch_rad   = pitch_rad;
            s_imu.roll_rad    = roll_rad;
            s_imu.yaw_rad     = yaw_rad;
            s_imu.accel_x     = raw.accel_x;
            s_imu.accel_y     = raw.accel_y;
            s_imu.accel_z     = raw.accel_z;
            s_imu.gyro_x      = raw.gyro_x;
            s_imu.gyro_y      = raw.gyro_y;
            s_imu.gyro_z      = raw.gyro_z;
            s_imu.temperature = raw.temperature;
            s_imu.sample_ts   = now;
            s_imu.fresh       = true;
            imu_unlock();
        }
        /* else: no new data this cycle — skip and retry next period */

        vTaskDelayUntil(&last_wake, period);
    }

    vTaskDelete(NULL);
}

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void imu_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    memset(&s_imu, 0, sizeof(s_imu));

    if (!qmi8658_init()) {
        ESP_LOGE(TAG, "QMI8658 init failed — IMU will be unavailable");
        return;
    }

    /* Calibrate gyro while stationary. */
    qmi8658_calibrate_gyro(200);

    ESP_LOGI(TAG, "IMU subsystem ready");
}

void imu_start(void)
{
    if (s_task_running) {
        return;
    }
    s_task_running = true;
    BaseType_t ret = xTaskCreate(imu_task, "imu_task", 3072, NULL,
                                   configMAX_PRIORITIES - 2, &s_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create IMU task");
        s_task_running = false;
    } else {
        ESP_LOGI(TAG, "IMU polling task started (100 Hz)");
    }
}

void imu_stop(void)
{
    s_task_running = false;
    /* Task will delete itself on next iteration. */
}

void imu_get_snapshot(imu_snapshot_t *snap)
{
    if (!snap) {
        return;
    }
    imu_lock();
    snap->pitch_deg   = s_imu.pitch_rad * 180.0f / M_PI;
    snap->roll_deg    = s_imu.roll_rad  * 180.0f / M_PI;
    snap->yaw_deg     = s_imu.yaw_rad   * 180.0f / M_PI;
    snap->pitch_rad   = s_imu.pitch_rad;
    snap->roll_rad    = s_imu.roll_rad;
    snap->yaw_rad     = s_imu.yaw_rad;
    snap->accel_x     = s_imu.accel_x;
    snap->accel_y     = s_imu.accel_y;
    snap->accel_z     = s_imu.accel_z;
    snap->gyro_x      = s_imu.gyro_x;
    snap->gyro_y      = s_imu.gyro_y;
    snap->gyro_z      = s_imu.gyro_z;
    snap->temperature = s_imu.temperature;
    snap->sample_ts   = s_imu.sample_ts;
    snap->fresh       = s_imu.fresh;
    imu_unlock();
}

void imu_calibrate(void)
{
    qmi8658_calibrate_gyro(200);
}

bool imu_is_fresh(void)
{
    imu_lock();
    bool fresh = s_imu.fresh
              && (lv_tick_get() - s_imu.sample_ts) < IMU_STALE_MS;
    imu_unlock();
    return fresh;
}
