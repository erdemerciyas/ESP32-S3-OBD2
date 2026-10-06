#include "clock.h"
#include "app_log.h"
#include "driver/i2c.h"
#include "nvs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

static const char *TAG = "clock";

#define NVS_NS        "clock"
#define NVS_KEY_TZ    "tz"
#define NVS_KEY_SAVER "saver"
#define TZ_DEFAULT    180            /* Türkiye UTC+3 */
#define VALID_EPOCH   1700000000LL   /* 2023-11: bundan eskiyse ayarlanmamış */

/* PCF85063 — dokunmatik / IMU ile aynı I2C hattı (port 0). Yoksa sessizce atlanır. */
#define RTC_PORT      0
#define RTC_ADDR      0x51
#define RTC_REG_SEC   0x04
#define RTC_TIMEOUT   pdMS_TO_TICKS(50)

static int  s_tz_min = TZ_DEFAULT;
static bool s_saver = true;
static bool s_rtc_ok;

static void apply_tz(int tz_min)
{
    /* POSIX TZ işareti terstir: UTC+3 → "UTC-3:00" */
    char tz[16];
    int a = abs(tz_min);
    snprintf(tz, sizeof(tz), "UTC%c%d:%02d", tz_min >= 0 ? '-' : '+', a / 60, a % 60);
    setenv("TZ", tz, 1);
    tzset();
}

static void nvs_save_i16(const char *key, int16_t v)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i16(h, key, v);
        nvs_commit(h);
        nvs_close(h);
    }
}

static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t bin2bcd(int v)     { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

/* RTC'de UTC tutulur. */
static bool rtc_read(time_t *out)
{
    uint8_t reg = RTC_REG_SEC, d[7];
    if (i2c_master_write_read_device(RTC_PORT, RTC_ADDR, &reg, 1, d, sizeof(d), RTC_TIMEOUT) != ESP_OK) {
        return false;
    }
    s_rtc_ok = true;
    if (d[0] & 0x80) {
        return false;   /* OS: osilatör durmuş, zaman geçersiz */
    }
    struct tm tm = {
        .tm_sec  = bcd2bin(d[0] & 0x7F),
        .tm_min  = bcd2bin(d[1] & 0x7F),
        .tm_hour = bcd2bin(d[2] & 0x3F),
        .tm_mday = bcd2bin(d[3] & 0x3F),
        .tm_mon  = bcd2bin(d[5] & 0x1F) - 1,
        .tm_year = bcd2bin(d[6]) + 100,
    };
    /* mktime yerel saat bekler; RTC UTC — TZ'yi geçici UTC0 yap */
    setenv("TZ", "UTC0", 1);
    tzset();
    *out = mktime(&tm);
    apply_tz(s_tz_min);
    return *out > VALID_EPOCH;
}

static void rtc_write(time_t t)
{
    struct tm tm;
    gmtime_r(&t, &tm);
    uint8_t d[8] = {
        RTC_REG_SEC,
        bin2bcd(tm.tm_sec), bin2bcd(tm.tm_min), bin2bcd(tm.tm_hour), bin2bcd(tm.tm_mday),
        (uint8_t)tm.tm_wday, bin2bcd(tm.tm_mon + 1), bin2bcd(tm.tm_year % 100),
    };
    if (i2c_master_write_to_device(RTC_PORT, RTC_ADDR, d, sizeof(d), RTC_TIMEOUT) != ESP_OK) {
        app_log_warn(TAG, "RTC write failed");
    }
}

void clock_init(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        int16_t v;
        if (nvs_get_i16(h, NVS_KEY_TZ, &v) == ESP_OK && v >= -720 && v <= 840) {
            s_tz_min = v;
        }
        if (nvs_get_i16(h, NVS_KEY_SAVER, &v) == ESP_OK) {
            s_saver = v != 0;
        }
        nvs_close(h);
    }
    apply_tz(s_tz_min);

    time_t t;
    if (!clock_is_valid() && rtc_read(&t)) {
        struct timeval tv = { .tv_sec = t };
        settimeofday(&tv, NULL);
        app_log_info(TAG, "Time from RTC");
    }
    app_log_info(TAG, "RTC %s, TZ %+d min, saver %s", s_rtc_ok ? "found" : "absent",
                 s_tz_min, s_saver ? "on" : "off");
}

void clock_set_from_phone(int64_t epoch_s, int tz_min)
{
    if (epoch_s < VALID_EPOCH) {
        return;
    }
    if (tz_min >= -720 && tz_min <= 840 && tz_min != s_tz_min) {
        s_tz_min = tz_min;
        apply_tz(tz_min);
        nvs_save_i16(NVS_KEY_TZ, (int16_t)tz_min);
    }
    /* 2 sn'den küçük sapmada dokunma (ping her geldiğinde saniye zıplamasın) */
    int64_t drift = epoch_s - (int64_t)time(NULL);
    if (drift > -2 && drift < 2) {
        return;
    }
    struct timeval tv = { .tv_sec = (time_t)epoch_s };
    settimeofday(&tv, NULL);
    if (s_rtc_ok) {
        rtc_write((time_t)epoch_s);
    }
    app_log_info(TAG, "Time synced from phone (drift %llds)", (long long)drift);
}

bool clock_is_valid(void)
{
    return (int64_t)time(NULL) > VALID_EPOCH;
}

bool clock_now(struct tm *out)
{
    time_t t = time(NULL);
    localtime_r(&t, out);
    return (int64_t)t > VALID_EPOCH;
}

const char *clock_day_name(int wday)
{
    static const char *const DAYS[] = {
        "PAZAR", "PAZARTES\xC4\xB0", "SALI", "\xC3\x87" "AR\xC5\x9E" "AMBA",
        "PER\xC5\x9E" "EMBE", "CUMA", "CUMARTES\xC4\xB0",
    };
    return DAYS[(unsigned)wday % 7];
}

const char *clock_month_name(int mon)
{
    static const char *const MONTHS[] = {
        "OCAK", "\xC5\x9E" "UBAT", "MART", "N\xC4\xB0SAN", "MAYIS", "HAZ\xC4\xB0RAN",
        "TEMMUZ", "A\xC4\x9EUSTOS", "EYL\xC3\x9CL", "EK\xC4\xB0M", "KASIM", "ARALIK",
    };
    return MONTHS[(unsigned)mon % 12];
}

bool clock_saver_enabled(void)
{
    return s_saver;
}

void clock_saver_set(bool on)
{
    s_saver = on;
    nvs_save_i16(NVS_KEY_SAVER, on ? 1 : 0);
}
