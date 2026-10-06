#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ROLL kişisel ayarları — telefondaki "ROLL ayarları" ekranında düzenlenir,
 * "rcfg" mesajıyla gelir, NVS'de ("roll"/"cfg") saklanır. Hızlar her zaman
 * km/h, mesafeler metre (birim yalnız gösterim). */

#define ROLL_MAX_SPD   10
#define ROLL_MAX_DST   6
#define ROLL_MAX_BRK   3
#define ROLL_NAME_LEN  24

typedef enum {
    ROLL_SRC_MODE_AUTO = 0,    /* GPS öncelikli, yoksa OBD */
    ROLL_SRC_MODE_GPS,
    ROLL_SRC_MODE_OBD,
} roll_src_mode_t;

typedef struct {
    uint16_t from, to;         /* km/h */
} roll_range_t;

typedef struct {
    uint8_t      version;
    uint8_t      src;          /* roll_src_mode_t */
    bool         mph;
    uint8_t      n_spd, n_dst, n_brk;
    roll_range_t spd[ROLL_MAX_SPD];
    float        dst[ROLL_MAX_DST];      /* m */
    roll_range_t brk[ROLL_MAX_BRK];      /* from → to (0) */
    bool         tree;         /* geri sayım ışıkları (değilse otomatik hazır) */
    float        tree_s;       /* ışıklar arası (0.4 pro / 0.5 sportsman) */
    bool         rollout;      /* 1 ft rollout */
    float        slope_max;    /* % ; 0 = kapalı */
    bool         slope_corr;   /* eğim düzeltmeli süreyi de göster */
    float        gps_acc_max;  /* m/s */
    uint8_t      sats_min;
    float        obd_k;        /* OBD hız çarpanı; 0 = GPS'ten öğren */
    uint16_t     mass_kg;
    uint8_t      drive;        /* 0 fwd, 1 rwd, 2 awd */
    char         name[ROLL_NAME_LEN];
    bool         beep;
    uint8_t      hold_s;       /* sonuç ekranda; 0 = dokununca */
} roll_cfg_t;

void roll_cfg_init(void);
/* Kopya döner (görevler arası güvenli). */
void roll_cfg_get(roll_cfg_t *out);
/* Doğrular, kaydeder; false: geçersiz (why doldurulur). */
bool roll_cfg_set(const roll_cfg_t *cfg, const char **why);
/* rcfg JSON'u → ayar (eksik alanlar mevcut değerle kalır). */
bool roll_cfg_parse_json(const char *json, size_t len, roll_cfg_t *out);
uint32_t roll_cfg_rev(void);
