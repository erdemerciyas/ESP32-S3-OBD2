#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* Duvar saati: telefon (NAV bağlantısı) zamanı gönderince ayarlanır; kartta
 * PCF85063 RTC varsa oraya da yazılır ve açılışta oradan okunur. Saat
 * dilimi ve ekran koruyucu ayarı NVS'de. */

void clock_init(void);
/* Telefon: UTC epoch saniye + yerel ofset (dakika, ör. +180) */
void clock_set_from_phone(int64_t epoch_s, int tz_min);
/* Saat geçerli mi (telefon / RTC ile en az bir kez ayarlandı) */
bool clock_is_valid(void);
/* Yerel saat; false: henüz ayarlanmadı */
bool clock_now(struct tm *out);

/* Türkçe büyük harf gün / ay adı (UTF-8) */
const char *clock_day_name(int wday);
const char *clock_month_name(int mon);

bool clock_saver_enabled(void);
void clock_saver_set(bool on);
