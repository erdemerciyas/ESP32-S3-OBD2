#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Arıza kodu (DTC) tarama / silme / kayıt.
 *
 * Tarama obd_poll görevinde adım adım yürür (0101 → 03 → 07 → 02 FF → 0121/0131);
 * çalışırken normal PID polling'i duraklar, böylece K-line'da tek komut uçuşta
 * kalır. Sonuçlar mutex altında tutulur, UI kopya alır (obd_dtc_get_report).
 *
 * Kod gösterimi SAE J2012 ham 16 bit: üst 2 bit sistem (P/C/B/U), kalan 14 bit
 * 4 hane. Örn. P0133 = 0x0133, P1626 = 0x1626, U0100 = 0xC100.
 */

#define DTC_MAX          16
#define DTC_HIST_MAX     24

#define DTC_KIND_STORED  0x01   /* Mode 03: kayıtlı (onaylanmış) */
#define DTC_KIND_PENDING 0x02   /* Mode 07: beklemede (tek sürüşte görüldü) */
#define DTC_KIND_CLEARED 0x04   /* geçmiş: bu koddan sonra silme yapıldı */

typedef enum {
    DTC_ST_IDLE = 0,     /* hiç taranmadı */
    DTC_ST_SCANNING,
    DTC_ST_CLEARING,
    DTC_ST_DONE,         /* son tarama başarılı */
    DTC_ST_CLEARED,      /* silme doğrulandı */
    DTC_ST_CLEAR_FAIL,   /* silme sonrası kod hâlâ var / ECU reddetti */
    DTC_ST_ERROR,        /* ECU yanıt vermedi */
} dtc_status_t;

typedef struct {
    uint16_t code;
    uint8_t  kind;
} dtc_entry_t;

typedef struct {
    bool     valid;
    uint16_t code;          /* donmuş veriyi tetikleyen kod */
    float    rpm, speed, coolant, load, stft, ltft, map;   /* NAN = yok */
} dtc_freeze_t;

typedef struct {
    dtc_status_t status;
    uint8_t  progress;      /* 0..100 */
    bool     from_flash;    /* bağlantı yok: son kayıttan gösteriliyor */
    bool     monitor_valid; /* 0101 okundu */
    bool     mil_on;
    uint8_t  ecu_dtc_count; /* 0101'in bildirdiği kayıtlı kod sayısı */
    uint8_t  ready_ok;      /* hazırlık monitörleri: tamamlanan */
    uint8_t  ready_total;   /* desteklenen */
    int32_t  km_mil;        /* 0121: MIL yanarken km, -1 = yok */
    int32_t  km_clear;      /* 0131: son silmeden beri km, -1 = yok */
    uint8_t  count;
    dtc_entry_t codes[DTC_MAX];
    dtc_freeze_t ff;
    uint16_t scan_no;
    uint32_t seq;           /* her değişimde artar (UI yenileme) */
} dtc_report_t;

typedef struct {
    uint16_t code;
    uint8_t  kind;          /* DTC_KIND_* birikimli */
    uint8_t  hits;          /* görüldüğü tarama sayısı */
    uint16_t first_scan;
    uint16_t last_scan;
} dtc_hist_item_t;

typedef struct {
    uint8_t  version;
    uint8_t  count;
    uint16_t scan_no;       /* kaydedilen tarama sayısı */
    uint16_t clear_no;      /* yapılan silme sayısı */
    uint16_t last_clear_scan;
    dtc_hist_item_t items[DTC_HIST_MAX];
} dtc_hist_t;

void obd_dtc_init(void);

/* UI → OBD istekleri. false: bağlantı yok ya da iş zaten sürüyor. */
bool obd_dtc_request_scan(void);
bool obd_dtc_request_clear(void);
void obd_dtc_erase_history(void);

void obd_dtc_get_report(dtc_report_t *out);
void obd_dtc_get_history(dtc_hist_t *out);
uint32_t obd_dtc_seq(void);
/* Dash göstergesi için: kayıtlı kod sayısı (MIL yanıyorsa en az 1). */
uint8_t obd_dtc_active_count(bool *mil_on);

/* obd_poll görevinden çağrılır. */
void obd_dtc_on_link_ready(void);     /* PID keşfi bitti: otomatik tarama */
void obd_dtc_on_disconnect(void);
bool obd_dtc_service(uint32_t now);   /* true: DTC işi aktif, polling'i duraklat */

/* Kodu "P0133" biçimine çevirir (buf >= 6). */
void obd_dtc_format(uint16_t code, char *buf);
