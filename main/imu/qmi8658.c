#include "qmi8658.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static const char *TAG = "qmi8658";

/* Detected I2C address — set during init. */
static uint8_t s_i2c_addr = QMI8658_I2C_ADDR_DEFAULT;
static bool    s_init_ok  = false;

/* Static gyro bias offsets — filled by calibration, applied on every read. */
static float s_gyro_bias_x = 0.0f;
static float s_gyro_bias_y = 0.0f;
static float s_gyro_bias_z = 0.0f;

/* Conversion factors — set during init based on full-scale ranges. */
static float s_accel_lsb_to_ms2 = 0.0f;
static float s_gyro_lsb_to_rad  = 0.0f;

/* ---------------------------------------------------------------------------
 * Low-level I2C helpers
 * ------------------------------------------------------------------------- */

static bool i2c_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    esp_err_t ret = i2c_master_write_to_device(
        QMI8658_I2C_MASTER_PORT, s_i2c_addr,
        buf, sizeof(buf), pdMS_TO_TICKS(QMI8658_I2C_MASTER_TIMEOUT));
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "I2C w 0x%02X fail: %s", reg, esp_err_to_name(ret));
        return false;
    }
    return true;
}

static bool i2c_read_reg(uint8_t reg, uint8_t *value)
{
    esp_err_t ret = i2c_master_write_read_device(
        QMI8658_I2C_MASTER_PORT, s_i2c_addr,
        &reg, 1, value, 1, pdMS_TO_TICKS(QMI8658_I2C_MASTER_TIMEOUT));
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "I2C r 0x%02X fail: %s", reg, esp_err_to_name(ret));
        return false;
    }
    return true;
}

static bool i2c_read_regs(uint8_t start_reg, uint8_t *data, size_t len)
{
    esp_err_t ret = i2c_master_write_read_device(
        QMI8658_I2C_MASTER_PORT, s_i2c_addr,
        &start_reg, 1, data, len, pdMS_TO_TICKS(QMI8658_I2C_MASTER_TIMEOUT));
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "I2C br 0x%02X fail: %s", start_reg, esp_err_to_name(ret));
        return false;
    }
    return true;
}

/* ---------------------------------------------------------------------------
 * I2C scanner — try to talk to every address in the list.
 * Returns the address that ACKs (any register read succeeds), or 0 on failure.
 * ------------------------------------------------------------------------- */
static uint8_t i2c_scan(const uint8_t *addrs, int count)
{
    for (int i = 0; i < count; i++) {
        uint8_t reg = QMI8658_REG_WHO_AM_I;
        uint8_t val = 0;
        esp_err_t r = i2c_master_write_read_device(
            QMI8658_I2C_MASTER_PORT, addrs[i],
            &reg, 1, &val, 1, pdMS_TO_TICKS(100));
        if (r == ESP_OK) {
            ESP_LOGI(TAG, "I2C scan: device responds at 0x%02X (WHO_AM_I=0x%02X)",
                     addrs[i], val);
            return addrs[i];
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

bool qmi8658_init(void)
{
    /* --- 1. Use the I2C bus — already configured by bsp_display_init() ----
     * The touch driver owns I2C_NUM_0 on GPIO 19/20.  We must NOT call
     * i2c_param_config or i2c_driver_install — those corrupt the touch
     * controller's I2C communication.  Just use the bus as-is. */
    ESP_LOGI(TAG, "Using existing I2C bus (port %d, SDA=%d SCL=%d)",
             QMI8658_I2C_MASTER_PORT,
             QMI8658_I2C_MASTER_SDA_IO, QMI8658_I2C_MASTER_SCL_IO);

    /* --- 2. Scan I2C bus for the IMU ------------------------------------ */
    const uint8_t scan_addrs[] = { 0x6A, 0x6B, 0x68, 0x69 };
    uint8_t found_addr = i2c_scan(scan_addrs, sizeof(scan_addrs));

    if (found_addr == 0) {
        ESP_LOGE(TAG, "No IMU found on I2C bus (tried 0x6A,0x6B,0x68,0x69). "
                 "Check wiring: SDA=GPIO%d SCL=GPIO%d",
                 QMI8658_I2C_MASTER_SDA_IO, QMI8658_I2C_MASTER_SCL_IO);
        return false;
    }

    s_i2c_addr = found_addr;

    /* --- 3. Soft reset --------------------------------------------------- */
    i2c_write_reg(QMI8658_REG_CTRL9, QMI8658_CTRL9_SOFT_RESET);
    vTaskDelay(pdMS_TO_TICKS(30));

    /* --- 4. Read WHO_AM_I (informational only — we already have ACK) ---- */
    uint8_t who = 0;
    if (i2c_read_reg(QMI8658_REG_WHO_AM_I, &who)) {
        ESP_LOGI(TAG, "QMI8658 at 0x%02X, WHO_AM_I=0x%02X %s",
                 s_i2c_addr, who,
                 (who == QMI8658_WHO_AM_I_VAL) ? "(OK)" : "(unexpected)");
    }

    /* --- 5. Configure accelerometer: ±8g, 250 Hz ------------------------- */
    uint8_t ctrl1 = QMI8658_CTRL1_ODR_250HZ | QMI8658_CTRL1_FS_8G;
    if (!i2c_write_reg(QMI8658_REG_CTRL1, ctrl1)) {
        ESP_LOGE(TAG, "Failed to write CTRL1");
        return false;
    }
    s_accel_lsb_to_ms2 = (8.0f * 9.80665f) / 32768.0f;

    /* --- 6. Configure gyroscope: ±512 dps, 250 Hz ------------------------ */
    uint8_t ctrl2 = QMI8658_CTRL2_ODR_250HZ | QMI8658_CTRL2_FS_512DPS;
    if (!i2c_write_reg(QMI8658_REG_CTRL2, ctrl2)) {
        ESP_LOGE(TAG, "Failed to write CTRL2");
        return false;
    }
    s_gyro_lsb_to_rad = (512.0f * (M_PI / 180.0f)) / 32768.0f;

    /* --- 7. Enable both sensors (CTRL3) ---------------------------------- */
    uint8_t ctrl3 = QMI8658_CTRL3_ACCEL_EN | QMI8658_CTRL3_GYRO_EN
                  | QMI8658_CTRL3_GYRO_EXT_512;
    if (!i2c_write_reg(QMI8658_REG_CTRL3, ctrl3)) {
        ESP_LOGE(TAG, "Failed to write CTRL3");
        return false;
    }

    /* --- 8. Enable sensor block (CTRL7) ---------------------------------- */
    if (!i2c_write_reg(QMI8658_REG_CTRL7, QMI8658_CTRL7_ENABLE)) {
        ESP_LOGE(TAG, "Failed to write CTRL7");
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(50));

    s_init_ok = true;
    ESP_LOGI(TAG, "QMI8658 ready: accel ±8g @250Hz, gyro ±512dps @250Hz");
    return true;
}

void qmi8658_reset(void)
{
    i2c_write_reg(QMI8658_REG_CTRL9, QMI8658_CTRL9_SOFT_RESET);
    vTaskDelay(pdMS_TO_TICKS(20));
}

uint8_t qmi8658_read_id(void)
{
    uint8_t who = 0;
    i2c_read_reg(QMI8658_REG_WHO_AM_I, &who);
    return who;
}

bool qmi8658_read_sensors(qmi8658_data_t *data)
{
    if (!data || !s_init_ok) {
        return false;
    }

    /* Simple retry: try up to 3 times before giving up. */
    for (int retry = 0; retry < 3; retry++) {
        /* Burst-read 12 bytes starting at AX_L (0x35) through GZ_H (0x40). */
        uint8_t buf[12];
        if (!i2c_read_regs(QMI8658_REG_AX_L, buf, sizeof(buf))) {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }

        /* Quick sanity: if all bytes are 0x00 or 0xFF, chip is probably
         * not producing data — retry. */
        bool all_zero = true, all_ff = true;
        for (int i = 0; i < 12; i++) {
            if (buf[i] != 0x00) all_zero = false;
            if (buf[i] != 0xFF) all_ff  = false;
        }
        if (all_zero || all_ff) {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }

        /* Convert raw int16 (little-endian) → float in physical units. */
        int16_t raw_ax = (int16_t)(buf[0]  | (buf[1]  << 8));
        int16_t raw_ay = (int16_t)(buf[2]  | (buf[3]  << 8));
        int16_t raw_az = (int16_t)(buf[4]  | (buf[5]  << 8));
        int16_t raw_gx = (int16_t)(buf[6]  | (buf[7]  << 8));
        int16_t raw_gy = (int16_t)(buf[8]  | (buf[9]  << 8));
        int16_t raw_gz = (int16_t)(buf[10] | (buf[11] << 8));

        data->accel_x = (float)raw_ax * s_accel_lsb_to_ms2;
        data->accel_y = (float)raw_ay * s_accel_lsb_to_ms2;
        data->accel_z = (float)raw_az * s_accel_lsb_to_ms2;

        data->gyro_x = (float)raw_gx * s_gyro_lsb_to_rad - s_gyro_bias_x;
        data->gyro_y = (float)raw_gy * s_gyro_lsb_to_rad - s_gyro_bias_y;
        data->gyro_z = (float)raw_gz * s_gyro_lsb_to_rad - s_gyro_bias_z;

        /* Read temperature. */
        uint8_t temp_buf[2];
        if (i2c_read_regs(QMI8658_REG_TEMP_L, temp_buf, 2)) {
            int16_t raw_temp = (int16_t)(temp_buf[0] | (temp_buf[1] << 8));
            data->temperature = (float)raw_temp / 256.0f + 23.0f;
        } else {
            data->temperature = 0.0f;
        }

        return true;
    }

    return false;
}

void qmi8658_calibrate_gyro(int samples)
{
    if (!s_init_ok) return;
    if (samples < 50)  samples = 50;
    if (samples > 1000) samples = 1000;

    ESP_LOGI(TAG, "Calibrating gyro (%d samples) — keep device still...", samples);

    double sum_x = 0.0, sum_y = 0.0, sum_z = 0.0;
    int valid = 0;

    for (int i = 0; i < samples; i++) {
        qmi8658_data_t d;
        if (qmi8658_read_sensors(&d)) {
            sum_x += d.gyro_x + s_gyro_bias_x;
            sum_y += d.gyro_y + s_gyro_bias_y;
            sum_z += d.gyro_z + s_gyro_bias_z;
            valid++;
        }
        vTaskDelay(pdMS_TO_TICKS(8));
    }

    if (valid > 0) {
        s_gyro_bias_x = (float)(sum_x / valid);
        s_gyro_bias_y = (float)(sum_y / valid);
        s_gyro_bias_z = (float)(sum_z / valid);
        ESP_LOGI(TAG, "Gyro bias: x=%.6f y=%.6f z=%.6f rad/s",
                 s_gyro_bias_x, s_gyro_bias_y, s_gyro_bias_z);
    } else {
        ESP_LOGW(TAG, "Calibration: no valid samples — bias unchanged");
    }
}
