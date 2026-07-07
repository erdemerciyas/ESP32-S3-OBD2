#pragma once

#include <stdbool.h>
#include <stdint.h>

/* QMI8658 I2C address (AD0 pin = GND on most Waveshare boards) */
#define QMI8658_I2C_ADDR_DEFAULT    0x6A
#define QMI8658_I2C_ADDR_ALT        0x6B

/* I2C bus config — QMI8658 shares the touch I2C bus on Waveshare boards.
 * The bus is already configured by bsp_display_init() before we run.
 * We must NOT call i2c_param_config or i2c_driver_install — just reuse. */
#define QMI8658_I2C_MASTER_SCL_IO   20     /* SCL (shared with touch) */
#define QMI8658_I2C_MASTER_SDA_IO   19     /* SDA (shared with touch) */
#define QMI8658_I2C_MASTER_FREQ_HZ  400000
#define QMI8658_I2C_MASTER_PORT     0      /* I2C_NUM_0 — same as touch  */
#define QMI8658_I2C_MASTER_TIMEOUT  1000

/* --- Register map -------------------------------------------------------- */
#define QMI8658_REG_WHO_AM_I        0x00    /* RO, should return 0x05          */
#define QMI8658_REG_REVISION_ID     0x01    /* RO, silicon revision            */
#define QMI8658_REG_CTRL1           0x02    /* RW, accel ODR + full-scale       */
#define QMI8658_REG_CTRL2           0x03    /* RW, gyro ODR + full-scale        */
#define QMI8658_REG_CTRL3           0x04    /* RW, sensor enable + misc         */
#define QMI8658_REG_CTRL4           0x05    /* RW, sensor enable 2              */
#define QMI8658_REG_CTRL5           0x06    /* RW, low-pass / bandwidth         */
#define QMI8658_REG_CTRL6           0x07    /* RW, motion detection             */
#define QMI8658_REG_CTRL7           0x08    /* RW, enable / ack                 */
#define QMI8658_REG_CTRL8           0x09    /* RW, reserved                     */
#define QMI8658_REG_CTRL9           0x0A    /* W,  command (soft reset = 0xB8)  */
#define QMI8658_REG_STATUSINT       0x2D    /* RO, data-ready + interrupt flags */
#define QMI8658_REG_TEMP_L          0x33    /* RO, temperature low byte         */
#define QMI8658_REG_TEMP_H          0x34    /* RO, temperature high byte        */
#define QMI8658_REG_AX_L            0x35    /* RO, accel X low byte             */
#define QMI8658_REG_AX_H            0x36
#define QMI8658_REG_AY_L            0x37
#define QMI8658_REG_AY_H            0x38
#define QMI8658_REG_AZ_L            0x39
#define QMI8658_REG_AZ_H            0x3A
#define QMI8658_REG_GX_L            0x3B    /* RO, gyro X low byte              */
#define QMI8658_REG_GX_H            0x3C
#define QMI8658_REG_GY_L            0x3D
#define QMI8658_REG_GY_H            0x3E
#define QMI8658_REG_GZ_L            0x3F
#define QMI8658_REG_GZ_H            0x40

/* WHO_AM_I expected value */
#define QMI8658_WHO_AM_I_VAL        0x05

/* CTRL1 – Accelerometer configuration (default: 0x00)
 * [7:5] ODR:  000=8kHz, 001=4kHz, 010=2kHz, 011=1kHz,
 *              100=500Hz, 101=250Hz, 110=125Hz, 111=62.5Hz
 * [4:3] FS:   00=±2g, 01=±4g, 10=±8g, 11=±16g
 * [2:0] reserved */
#define QMI8658_CTRL1_ODR_125HZ     0x60    /* 110 << 5 = 0x60                  */
#define QMI8658_CTRL1_ODR_250HZ     0x50
#define QMI8658_CTRL1_ODR_500HZ     0x40
#define QMI8658_CTRL1_ODR_1KHZ      0x30
#define QMI8658_CTRL1_FS_2G         0x00    /* 00 << 3 = 0x00                   */
#define QMI8658_CTRL1_FS_4G         0x08    /* 01 << 3 = 0x08                   */
#define QMI8658_CTRL1_FS_8G         0x10    /* 10 << 3 = 0x10                   */
#define QMI8658_CTRL1_FS_16G        0x18    /* 11 << 3 = 0x18                   */

/* CTRL2 – Gyroscope configuration (default: 0x00)
 * [7:5] ODR:  same encoding as CTRL1
 * [4:3] FS:   00=±16dps, 01=±32dps, 10=±64dps, 11=±128dps
 *             NOTE — extended range via CTRL3[7:6] for ±256/512/1024/2048
 * [2:0] reserved */
#define QMI8658_CTRL2_ODR_125HZ     0x60
#define QMI8658_CTRL2_ODR_250HZ     0x50
#define QMI8658_CTRL2_ODR_500HZ     0x40
#define QMI8658_CTRL2_FS_512DPS     0x18    /* 11 with extended range via CTRL3 */

/* CTRL3 – Sensor enable / extended range
 * [7] gyro_ext_range_en
 * [6] gyro_ext_range: 0=±256dps, 1=±512dps (combined with CTRL2 FS)
 * [5] reserved
 * [4] accel_en: 1 = accelerometer on
 * [3] gyro_en:  1 = gyroscope on
 * [2:0] reserved */
#define QMI8658_CTRL3_ACCEL_EN      0x10
#define QMI8658_CTRL3_GYRO_EN       0x08
#define QMI8658_CTRL3_GYRO_EXT_512  0xC0    /* ext en + 512 dps                */

/* CTRL7 – Enable
 * [7:1] reserved
 * [0] ctrl7_enable: 1 = enable sensor block */
#define QMI8658_CTRL7_ENABLE        0x01

/* CTRL9 – Command register
 * 0xB8 = soft reset (triggers and self-clears)
 * 0x00 = no operation */
#define QMI8658_CTRL9_SOFT_RESET    0xB8

/* Sensitivity conversion factors.
 * At ±8g, 1 LSB = 8g/32768 = 0.244 mg → m/s²: * 9.80665 / 4096
 * Actual: accel_lsb_to_ms2 = 8.0f * 9.80665f / 32768.0f
 * At ±512 dps, 1 LSB = 512/32768 = 0.015625 dps → rad/s: * PI/180 / 64
 * Actual: gyro_lsb_to_rad = 512.0f * (M_PI / 180.0f) / 32768.0f */

/* --- Raw sensor data bundle (one-shot read) ------------------------------- */
typedef struct {
    float accel_x;      /* m/s² */
    float accel_y;
    float accel_z;
    float gyro_x;       /* rad/s */
    float gyro_y;
    float gyro_z;
    float temperature;  /* °C   */
} qmi8658_data_t;

/* --- Public API ---------------------------------------------------------- */

/**
 * @brief  Initialise the I2C master bus and the QMI8658 sensor.
 *         Configures accelerometer at ±8g / 250 Hz,
 *         gyroscope at ±512 dps / 250 Hz.
 * @return true on success, false if WHO_AM_I mismatch or I2C error.
 */
bool qmi8658_init(void);

/**
 * @brief  Soft-reset the QMI8658.
 */
void qmi8658_reset(void);

/**
 * @brief  Read WHO_AM_I register.
 * @return Register value, or 0x00 on error.
 */
uint8_t qmi8658_read_id(void);

/**
 * @brief  Burst-read all 12 sensor bytes (accel + gyro) and convert to
 *         physical units. Blocks ~200 µs.
 * @param  data  Output struct, filled on success.
 * @return true if fresh data was available and read.
 */
bool qmi8658_read_sensors(qmi8658_data_t *data);

/**
 * @brief  Collect `samples` readings while stationary, compute gyro bias
 *         offsets (stored internally, applied automatically on read).
 */
void qmi8658_calibrate_gyro(int samples);
