#include "roll_cfg.h"
#include "app_log.h"

#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "roll_cfg";
#define CFG_VERSION 1

static roll_cfg_t s_cfg;
static SemaphoreHandle_t s_lock;
static volatile uint32_t s_rev;

static void defaults(roll_cfg_t *c)
{
    memset(c, 0, sizeof(*c));
    c->version = CFG_VERSION;
    c->src = ROLL_SRC_MODE_AUTO;
    c->spd[0] = (roll_range_t){ 0, 100 };
    c->spd[1] = (roll_range_t){ 60, 120 };
    c->spd[2] = (roll_range_t){ 100, 200 };
    c->n_spd = 3;
    c->dst[0] = 18.288f;     /* 60 ft */
    c->dst[1] = 201.168f;    /* 1/8 mil */
    c->dst[2] = 402.336f;    /* 1/4 mil */
    c->n_dst = 3;
    c->brk[0] = (roll_range_t){ 100, 0 };
    c->n_brk = 1;
    c->tree_s = 0.4f;
    c->slope_max = 1.0f;
    c->slope_corr = true;
    c->gps_acc_max = 1.0f;
    c->sats_min = 6;
    c->mass_kg = 1100;
    snprintf(c->name, sizeof(c->name), "Aracım");
    c->beep = true;
    c->hold_s = 10;
}

static bool valid(const roll_cfg_t *c, const char **why)
{
    const char *w = NULL;
    if (c->src > ROLL_SRC_MODE_OBD) w = "src";
    else if (c->n_spd > ROLL_MAX_SPD || c->n_dst > ROLL_MAX_DST || c->n_brk > ROLL_MAX_BRK) w = "count";
    else if (c->tree_s < 0.2f || c->tree_s > 1.0f) w = "tree";
    else if (c->slope_max < 0 || c->slope_max > 10) w = "slope";
    else if (c->gps_acc_max <= 0 || c->gps_acc_max > 5) w = "gacc";
    else if (c->obd_k != 0 && (c->obd_k < 0.85f || c->obd_k > 1.15f)) w = "obdk";
    else if (c->mass_kg < 300 || c->mass_kg > 6000) w = "mass";
    for (int i = 0; !w && i < c->n_spd; i++) {
        if (c->spd[i].from >= c->spd[i].to || c->spd[i].to > 400) w = "spd";
    }
    for (int i = 0; !w && i < c->n_brk; i++) {
        if (c->brk[i].from <= c->brk[i].to || c->brk[i].from > 400) w = "brk";
    }
    for (int i = 0; !w && i < c->n_dst; i++) {
        if (c->dst[i] <= 0 || c->dst[i] > 5000) w = "dst";
    }
    if (why) *why = w;
    return w == NULL;
}

static void save(void)
{
    nvs_handle_t h;
    if (nvs_open("roll", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "cfg", &s_cfg, sizeof(s_cfg));
        nvs_commit(h);
        nvs_close(h);
    }
}

void roll_cfg_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    defaults(&s_cfg);
    nvs_handle_t h;
    if (nvs_open("roll", NVS_READONLY, &h) == ESP_OK) {
        roll_cfg_t c;
        size_t n = sizeof(c);
        if (nvs_get_blob(h, "cfg", &c, &n) == ESP_OK && n == sizeof(c) &&
            c.version == CFG_VERSION && valid(&c, NULL)) {
            c.name[ROLL_NAME_LEN - 1] = '\0';
            s_cfg = c;
        }
        nvs_close(h);
    }
    app_log_info(TAG, "%s: %u hız / %u mesafe / %u fren hedefi", s_cfg.name,
                 s_cfg.n_spd, s_cfg.n_dst, s_cfg.n_brk);
}

void roll_cfg_get(roll_cfg_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_cfg;
    xSemaphoreGive(s_lock);
}

bool roll_cfg_set(const roll_cfg_t *cfg, const char **why)
{
    if (!valid(cfg, why)) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool same = memcmp(&s_cfg, cfg, sizeof(s_cfg)) == 0;
    if (!same) {
        s_cfg = *cfg;
        s_cfg.version = CFG_VERSION;
        s_cfg.name[ROLL_NAME_LEN - 1] = '\0';
        save();
        s_rev++;
    }
    xSemaphoreGive(s_lock);
    if (!same) {
        app_log_info(TAG, "Updated: %s, %u hız / %u mesafe / %u fren hedefi", cfg->name,
                     cfg->n_spd, cfg->n_dst, cfg->n_brk);
    }
    return true;
}

uint32_t roll_cfg_rev(void)
{
    return s_rev;
}

/* --- rcfg JSON --------------------------------------------------------------- */

/* "0-100,60-120" → aralıklar; tanınmayan parçalar atlanır */
static uint8_t parse_ranges(const char *s, roll_range_t *out, int max)
{
    uint8_t n = 0;
    while (s && *s && n < max) {
        char *end;
        long a = strtol(s, &end, 10);
        if (end != s && *end == '-') {
            const char *b0 = end + 1;
            long b = strtol(b0, &end, 10);
            if (end != b0 && a >= 0 && b >= 0 && a <= 400 && b <= 400) {
                out[n++] = (roll_range_t){ (uint16_t)a, (uint16_t)b };
            }
        }
        s = strchr(end, ',');
        if (s) s++;
    }
    return n;
}

static uint8_t parse_floats(const char *s, float *out, int max)
{
    uint8_t n = 0;
    while (s && *s && n < max) {
        char *end;
        float v = strtof(s, &end);
        if (end != s && v > 0) {
            out[n++] = v;
        }
        s = strchr(end, ',');
        if (s) s++;
    }
    return n;
}

static bool num(const cJSON *r, const char *k, double *v)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(r, k);
    if (!cJSON_IsNumber(it)) return false;
    *v = it->valuedouble;
    return true;
}

static const char *str(const cJSON *r, const char *k)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(r, k);
    return cJSON_IsString(it) ? it->valuestring : NULL;
}

bool roll_cfg_parse_json(const char *json, size_t len, roll_cfg_t *c)
{
    cJSON *r = cJSON_ParseWithLength(json, len);
    if (!r) {
        return false;
    }
    roll_cfg_get(c);
    const char *s;
    double v;
    if ((s = str(r, "src"))) {
        c->src = strcmp(s, "gps") == 0 ? ROLL_SRC_MODE_GPS
               : strcmp(s, "obd") == 0 ? ROLL_SRC_MODE_OBD : ROLL_SRC_MODE_AUTO;
    }
    if ((s = str(r, "unit")))  c->mph = strcmp(s, "mph") == 0;
    if ((s = str(r, "spd")))   c->n_spd = parse_ranges(s, c->spd, ROLL_MAX_SPD);
    if ((s = str(r, "brk")))   c->n_brk = parse_ranges(s, c->brk, ROLL_MAX_BRK);
    if ((s = str(r, "dst")))   c->n_dst = parse_floats(s, c->dst, ROLL_MAX_DST);
    if ((s = str(r, "start"))) c->tree = strcmp(s, "tree") == 0;
    if ((s = str(r, "drive"))) c->drive = strcmp(s, "rwd") == 0 ? 1 : strcmp(s, "awd") == 0 ? 2 : 0;
    if ((s = str(r, "name"))) {
        /* UTF-8'i karakter ortasında kesme */
        size_t n = strlen(s);
        if (n >= ROLL_NAME_LEN) {
            n = ROLL_NAME_LEN - 1;
            while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
        }
        memcpy(c->name, s, n);
        c->name[n] = '\0';
    }
    if (num(r, "tree", &v))  c->tree_s = (float)v;
    if (num(r, "ro", &v))    c->rollout = v != 0;
    if (num(r, "slope", &v)) c->slope_max = (float)v;
    if (num(r, "slc", &v))   c->slope_corr = v != 0;
    if (num(r, "gacc", &v))  c->gps_acc_max = (float)v;
    if (num(r, "sats", &v))  c->sats_min = (uint8_t)(v < 0 ? 0 : v > 30 ? 30 : v);
    if (num(r, "obdk", &v))  c->obd_k = (float)v;
    if (num(r, "mass", &v))  c->mass_kg = (uint16_t)(v < 0 ? 0 : v > 60000 ? 60000 : v);
    if (num(r, "beep", &v))  c->beep = v != 0;
    if (num(r, "hold", &v))  c->hold_s = (uint8_t)(v < 0 ? 0 : v > 120 ? 120 : v);
    cJSON_Delete(r);
    return true;
}
