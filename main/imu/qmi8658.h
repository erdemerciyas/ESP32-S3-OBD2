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
#define QMI8658_REG_CTRL1           0x02    /* RW, serial interface: addr auto-inc, endian */
#define QMI8658_REG_CTRL2           0x03    /* RW, accel full-scale + ODR        */
#define QMI8658_REG_CTRL3           0x04    /* RW, gyro full-scale + ODR         */
#define QMI8658_REG_CTRL4           0x05    /* RW, sensor enable 2              */
#define QMI8658_REG_CTRL5           0x06    /* RW, low-pass filters (a + g)      */
#define QMI8658_REG_CTRL6           0x07    /* RW, motion detection             */
#define QMI8658_REG_CTRL7           0x08    /* RW, sensor enable: b0 accel, b1 gyro */
#define QMI8658_REG_CTRL8           0x09    /* RW, reserved                     */
#define QMI8658_REG_CTRL9           0x0A    /* W,  host command                  */
#define QMI8658_REG_RESET           0x60    /* W,  0xB0 = soft reset             */
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

/* Datasheet (QMI8658A/C) kayıtları — önceki tanımlar yanlış kayıtlara yazıyordu:
 * jiroskop hiç açılmıyor (CTRL7=0x01), ivme verisi big-endian geliyordu.
 *
 * CTRL1: b6 ADDR_AI (burst okuma için adres artırma), b5 BE (0 = little endian)
 * CTRL2: ivme — b6:4 aFS (0=±2g 1=±4g 2=±8g 3=±16g), b3:0 aODR
 * CTRL3: jiro — b6:4 gFS (0=±16 … 5=±512 … 7=±2048 dps), b3:0 gODR
 *        ODR kodu (6DOF): 3=1 kHz, 4=500 Hz, 5=250 Hz, 6=125 Hz, 7=62.5 Hz
 * CTRL5: b6:5 gLPF_MODE, b4 gLPF_EN, b2:1 aLPF_MODE, b0 aLPF_EN
 *        mode 3 = ODR'nin %13.37'si (~33 Hz, 250 Hz ODR)
 * CTRL7: b0 aEN, b1 gEN
 * RESET: 0xB0 yaz → yazılımsal sıfırlama */
#define QMI8658_CTRL1_ADDR_AI       0x40
#define QMI8658_ODR_250HZ           0x05
#define QMI8658_CTRL2_FS_8G         0x20
#define QMI8658_CTRL3_FS_512DPS     0x50
#define QMI8658_CTRL5_LPF_33HZ      0x77    /* gLPF mode3 + en, aLPF mode3 + en */
#define QMI8658_CTRL7_ACC_GYRO_EN   0x03
#define QMI8658_RESET_CMD           0xB0

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
