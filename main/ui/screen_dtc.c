#include "ui.h"
#include "theme.h"
#include "obd_dtc.h"
#include "dtc_db.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/*
 * Arıza kodu ekranı — "Supernova" düzeni (460x460 yuvarlak görüş alanı):
 *
 *   y  24..212  nova: ışın halkası + iki hale + ilerleme yayı + çekirdek
 *   y  222      durum satırı (MIL, hazırlık, km)
 *   y  244..360 kod kartları (dikey kaydırma)
 *   y  370..406 TARA / SİL / GEÇMİŞ
 *
 * Renk durumu anlatır: camgöbeği = taranıyor, yeşil = temiz, sarı = bekleyen,
 * turuncu = kayıtlı, kırmızı = kritik / MIL. Şok dalgası halkaları yalnız
 * tarama sırasında animasyonludur; geri kalan her şey statiktir ve yalnız
 * rapor değiştiğinde (seq) yeniden çizilir.
 */

#define NOVA_Y     (-112)   /* tab merkezine göre: çekirdek merkezi y = 118 */
#define CORE_D     108
#define ARC_D      124
#define HALO1_D    148
#define HALO2_D    174
#define RAYS_D     192
#define RAY_R_IN   78
#define RAY_COUNT  48
#define RING_MAX   190
#define RING_MS    1600

#define STATUS_Y   222
#define LIST_Y     244
#define LIST_W     330
#define LIST_H     116
#define CARD_H     54
#define BTN_Y      370
#define BTN_W      78
#define BTN_H      36

enum { CONFIRM_CLEAR = 1, CONFIRM_ERASE };

static lv_obj_t *s_halo[2];
static lv_obj_t *s_rays;
static lv_obj_t *s_arc;
static lv_obj_t *s_core;
static lv_obj_t *s_core_num;
static lv_obj_t *s_core_cap;
static lv_obj_t *s_ring[2];
static lv_obj_t *s_status;
static lv_obj_t *s_list;
static lv_obj_t *s_empty;
static lv_obj_t *s_btn_scan;
static lv_obj_t *s_btn_clear;
static lv_obj_t *s_btn_hist;

static lv_obj_t *s_detail;
static lv_obj_t *s_det_ring;
static lv_obj_t *s_det_code;
static lv_obj_t *s_det_meta;
static lv_obj_t *s_det_desc;
static lv_obj_t *s_det_hint;
static lv_obj_t *s_det_ff;

static lv_obj_t *s_hist;
static lv_obj_t *s_hist_sum;
static lv_obj_t *s_hist_list;

static lv_obj_t *s_confirm;
static lv_obj_t *s_conf_title;
static lv_obj_t *s_conf_body;
static int s_conf_mode;

static dtc_report_t s_rep;
static dtc_entry_t s_sorted[DTC_MAX];
static uint8_t s_sorted_n;
static uint32_t s_seq = UINT32_MAX;
static int8_t s_conn = -1;
static bool s_metric = true;
static lv_color_t s_nova_col;
static bool s_anim_on;

/* ---------- yardımcılar ---------- */

static lv_obj_t *mk_plain(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_GESTURE_BUBBLE);
    return o;
}

static lv_obj_t *mk_circle(lv_obj_t *parent, lv_coord_t d)
{
    lv_obj_t *o = mk_plain(parent);
    lv_obj_set_size(o, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(o, LV_ALIGN_CENTER, 0, NOVA_Y);
    return o;
}

static lv_obj_t *mk_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, "");
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

static lv_obj_t *mk_button(lv_obj_t *parent, const char *text, lv_color_t accent, bool filled,
                           lv_event_cb_t cb, void *user_data)
{
    const ui_theme_t *t = theme_get();
    lv_obj_t *b = mk_plain(parent);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(b, BTN_W, BTN_H);
    lv_obj_set_style_radius(b, BTN_H / 2, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, accent, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, filled ? accent : t->surface, 0);
    lv_obj_set_style_bg_color(b, filled ? t->text : t->surface_hi, LV_STATE_PRESSED);
    lv_obj_set_style_opa(b, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *l = mk_label(b, t->font_tr_sm, filled ? t->bg : accent);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return b;
}

static lv_obj_t *mk_overlay(lv_obj_t *parent)
{
    const ui_theme_t *t = theme_get();
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, UI_VIEWPORT_SZ, UI_VIEWPORT_SZ);
    lv_obj_set_style_bg_color(o, t->bg, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_FLOATING);
    /* Açıkken yatay kaydırma sekme değiştirmesin. */
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN_HOR |
                         LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_GESTURE_BUBBLE);
    return o;
}

static void set_enabled(lv_obj_t *o, bool en)
{
    if (en) {
        lv_obj_clear_state(o, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(o, LV_STATE_DISABLED);
    }
}

static lv_color_t code_color(const ui_theme_t *t, dtc_sev_t sev, uint8_t kind)
{
    if (!(kind & DTC_KIND_STORED)) {
        return t->warn;                     /* yalnız bekleyen */
    }
    switch (sev) {
    case DTC_SEV_CRIT: return t->crit;
    case DTC_SEV_INFO: return t->primary;
    default:           return t->secondary;
    }
}

static const char *kind_text(uint8_t kind)
{
    if ((kind & DTC_KIND_STORED) && (kind & DTC_KIND_PENDING)) return "KAYITLI+";
    if (kind & DTC_KIND_STORED) return "KAYITLI";
    return "BEKLEYEN";
}

static const char *sev_text(dtc_sev_t sev)
{
    switch (sev) {
    case DTC_SEV_CRIT: return "KRİTİK";
    case DTC_SEV_INFO: return "BİLGİ";
    default:           return "UYARI";
    }
}

static bool busy(dtc_status_t st)
{
    return st == DTC_ST_SCANNING || st == DTC_ST_CLEARING;
}

/* ---------- nova ---------- */

static void rays_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    lv_coord_t cx = (a.x1 + a.x2) / 2;
    lv_coord_t cy = (a.y1 + a.y2) / 2;

    static const uint8_t len[4] = { 14, 5, 9, 5 };
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = s_nova_col;
    dsc.width = 2;
    dsc.round_start = 1;
    dsc.round_end = 1;

    for (int i = 0; i < RAY_COUNT; i++) {
        int16_t ang = (int16_t)(i * 360 / RAY_COUNT);
        int32_t s = lv_trigo_sin(ang);
        int32_t c = lv_trigo_cos(ang);
        lv_coord_t r2 = RAY_R_IN + len[i & 3];
        lv_point_t p1 = { (lv_coord_t)(cx + ((c * RAY_R_IN) >> LV_TRIGO_SHIFT)),
                          (lv_coord_t)(cy + ((s * RAY_R_IN) >> LV_TRIGO_SHIFT)) };
        lv_point_t p2 = { (lv_coord_t)(cx + ((c * r2) >> LV_TRIGO_SHIFT)),
                          (lv_coord_t)(cy + ((s * r2) >> LV_TRIGO_SHIFT)) };
        dsc.opa = (i & 3) == 0 ? LV_OPA_80 : LV_OPA_30;
        lv_draw_line(ctx, &dsc, &p1, &p2);
    }
}

static void ring_anim_cb(void *var, int32_t v)
{
    lv_obj_t *o = (lv_obj_t *)var;
    lv_coord_t d = (lv_coord_t)(CORE_D + (RING_MAX - CORE_D) * v / 1000);
    lv_obj_set_size(o, d, d);
    lv_obj_set_style_border_opa(o, (lv_opa_t)(220 - 220 * v / 1000), 0);
}

static void set_scan_anim(bool on)
{
    if (on == s_anim_on) {
        return;
    }
    s_anim_on = on;
    for (int i = 0; i < 2; i++) {
        lv_anim_del(s_ring[i], ring_anim_cb);
        if (!on) {
            lv_obj_add_flag(s_ring[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_set_size(s_ring[i], CORE_D, CORE_D);
        lv_obj_clear_flag(s_ring[i], LV_OBJ_FLAG_HIDDEN);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_ring[i]);
        lv_anim_set_exec_cb(&a, ring_anim_cb);
        lv_anim_set_values(&a, 0, 1000);
        lv_anim_set_time(&a, RING_MS);
        lv_anim_set_delay(&a, i * (RING_MS / 2));
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
    }
}

static void set_nova_color(lv_color_t col)
{
    const ui_theme_t *t = theme_get();
    s_nova_col = col;
    lv_obj_set_style_bg_color(s_halo[0], col, 0);
    lv_obj_set_style_bg_color(s_halo[1], col, 0);
    lv_obj_set_style_bg_color(s_core, lv_color_mix(col, t->bg, LV_OPA_30), 0);
    lv_obj_set_style_border_color(s_core, col, 0);
    lv_obj_set_style_arc_color(s_arc, col, LV_PART_INDICATOR);
    lv_obj_set_style_border_color(s_ring[0], col, 0);
    lv_obj_set_style_border_color(s_ring[1], col, 0);
    lv_obj_invalidate(s_rays);
}

/* ---------- liste ---------- */

static void sort_codes(void)
{
    s_sorted_n = s_rep.count;
    memcpy(s_sorted, s_rep.codes, sizeof(dtc_entry_t) * s_sorted_n);
    /* Kayıtlı önce, sonra önem derecesi. n <= 16: ekleme sıralaması yeter. */
    for (int i = 1; i < s_sorted_n; i++) {
        dtc_entry_t key = s_sorted[i];
        dtc_info_t ki;
        dtc_db_lookup(key.code, &ki);
        int kscore = ((key.kind & DTC_KIND_STORED) ? 10 : 0) + ki.sev;
        int j = i - 1;
        while (j >= 0) {
            dtc_info_t ji;
            dtc_db_lookup(s_sorted[j].code, &ji);
            int jscore = ((s_sorted[j].kind & DTC_KIND_STORED) ? 10 : 0) + ji.sev;
            if (jscore >= kscore) break;
            s_sorted[j + 1] = s_sorted[j];
            j--;
        }
        s_sorted[j + 1] = key;
    }
}

static void show_detail(int idx);

static void card_click_cb(lv_event_t *e)
{
    show_detail((int)(intptr_t)lv_event_get_user_data(e));
}

static void build_list(void)
{
    const ui_theme_t *t = theme_get();
    lv_obj_clean(s_list);
    lv_obj_scroll_to_y(s_list, 0, LV_ANIM_OFF);

    for (int i = 0; i < s_sorted_n; i++) {
        dtc_info_t info;
        dtc_db_lookup(s_sorted[i].code, &info);
        lv_color_t col = code_color(t, info.sev, s_sorted[i].kind);
        char code[6];
        obd_dtc_format(s_sorted[i].code, code);

        lv_obj_t *card = mk_plain(s_list);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER);
        lv_obj_set_size(card, LIST_W, CARD_H);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(card, t->surface, 0);
        lv_obj_set_style_bg_color(card, t->surface_hi, LV_STATE_PRESSED);
        lv_obj_set_style_bg_grad_color(card, lv_color_mix(col, t->surface, LV_OPA_20), 0);
        lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, lv_color_mix(col, t->border, LV_OPA_40), 0);
        lv_obj_add_event_cb(card, card_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *bar = mk_plain(card);
        lv_obj_set_size(bar, 4, CARD_H - 18);
        lv_obj_set_style_radius(bar, 2, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bar, col, 0);
        lv_obj_align(bar, LV_ALIGN_LEFT_MID, 8, 0);

        lv_obj_t *lc = mk_label(card, t->font_md, col);
        lv_label_set_text(lc, code);
        lv_obj_align(lc, LV_ALIGN_TOP_LEFT, 20, 5);

        lv_obj_t *ls = mk_label(card, t->font_tr_sm, t->text_dim);
        lv_label_set_text(ls, info.system);
        lv_obj_align_to(ls, lc, LV_ALIGN_OUT_RIGHT_MID, 8, 1);

        lv_obj_t *chip = mk_plain(card);
        lv_obj_set_size(chip, LV_SIZE_CONTENT, 20);
        lv_obj_set_style_radius(chip, 10, 0);
        lv_obj_set_style_pad_hor(chip, 8, 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_20, 0);
        lv_obj_set_style_bg_color(chip, col, 0);
        lv_obj_align(chip, LV_ALIGN_TOP_RIGHT, -8, 6);
        lv_obj_t *lk = mk_label(chip, t->font_tr_sm, col);
        lv_label_set_text(lk, kind_text(s_sorted[i].kind));
        lv_obj_center(lk);

        lv_obj_t *ld = mk_label(card, t->font_tr_sm, t->text);
        lv_label_set_long_mode(ld, LV_LABEL_LONG_DOT);
        lv_obj_set_width(ld, LIST_W - 34);
        lv_label_set_text(ld, info.desc);
        lv_obj_align(ld, LV_ALIGN_TOP_LEFT, 20, 30);
    }

    if (s_sorted_n == 0) {
        lv_obj_clear_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---------- durum ---------- */

static lv_color_t report_color(const ui_theme_t *t)
{
    switch (s_rep.status) {
    case DTC_ST_IDLE:     return t->text_dim;
    case DTC_ST_SCANNING:
    case DTC_ST_CLEARING: return t->primary;
    case DTC_ST_ERROR:    return t->warn;
    default:              break;
    }
    if (s_rep.count == 0) {
        return (s_rep.monitor_valid && s_rep.mil_on) ? t->crit : t->ok;
    }
    dtc_info_t info;
    dtc_db_lookup(s_sorted[0].code, &info);   /* sıralı: en ciddi başta */
    if (s_rep.monitor_valid && s_rep.mil_on) {
        return t->crit;
    }
    return code_color(t, info.sev, s_sorted[0].kind);
}

static void refresh_core(const ui_theme_t *t)
{
    char num[12];
    const char *cap;
    const lv_font_t *font = t->font_xl;

    switch (s_rep.status) {
    case DTC_ST_SCANNING:
    case DTC_ST_CLEARING:
        snprintf(num, sizeof(num), "%%%u", s_rep.progress);
        font = t->font_lg;
        cap = s_rep.status == DTC_ST_SCANNING ? "TARAMA" : "SİLME";
        break;
    case DTC_ST_IDLE:
        snprintf(num, sizeof(num), "--");
        cap = "HAZIR";
        break;
    case DTC_ST_ERROR:
        snprintf(num, sizeof(num), "!");
        cap = "YANIT YOK";
        break;
    case DTC_ST_CLEARED:
        snprintf(num, sizeof(num), "0");
        cap = "SİLİNDİ";
        break;
    default:
        snprintf(num, sizeof(num), "%u", s_rep.count);
        if (s_rep.status == DTC_ST_CLEAR_FAIL) {
            cap = "HATA";
        } else if (s_rep.count == 0) {
            cap = (s_rep.monitor_valid && s_rep.mil_on) ? "MIL AÇIK" : "TEMİZ";
        } else {
            cap = "ARIZA";
        }
        break;
    }
    lv_obj_set_style_text_font(s_core_num, font, 0);
    lv_label_set_text(s_core_num, num);
    lv_label_set_text(s_core_cap, cap);
    lv_arc_set_value(s_arc, busy(s_rep.status) ? s_rep.progress : 100);
    lv_obj_set_style_arc_opa(s_arc, busy(s_rep.status) ? LV_OPA_COVER : LV_OPA_50,
                             LV_PART_INDICATOR);
}

static void refresh_status(bool connected)
{
    char buf[96];
    int n = 0;

    switch (s_rep.status) {
    case DTC_ST_SCANNING:
        snprintf(buf, sizeof(buf), "ECU taranıyor, lütfen bekleyin");
        break;
    case DTC_ST_CLEARING:
        snprintf(buf, sizeof(buf), "Kodlar siliniyor...");
        break;
    case DTC_ST_ERROR:
        snprintf(buf, sizeof(buf), "ECU yanıt vermedi - tekrar dene");
        break;
    case DTC_ST_CLEAR_FAIL:
        snprintf(buf, sizeof(buf), "Silinemedi: kontak açık, motor kapalı olmalı");
        break;
    case DTC_ST_IDLE:
        snprintf(buf, sizeof(buf), connected ? "Taramak için TARA'ya dokun"
                                             : "Adaptör bağlı değil");
        break;
    default:
        if (s_rep.from_flash || !s_rep.monitor_valid) {
            n = snprintf(buf, sizeof(buf), "Kayıttan  •  Tarama #%u", s_rep.scan_no);
            if (!connected) {
                snprintf(buf + n, sizeof(buf) - n, "  •  Bağlantı yok");
            }
            break;
        }
        n = snprintf(buf, sizeof(buf), "MIL %s", s_rep.mil_on ? "AÇIK" : "KAPALI");
        if (s_rep.ready_total) {
            n += snprintf(buf + n, sizeof(buf) - n, "  •  Hazırlık %u/%u",
                          s_rep.ready_ok, s_rep.ready_total);
        }
        if (s_rep.km_mil > 0) {
            snprintf(buf + n, sizeof(buf) - n, "  •  MIL ile %ld km", (long)s_rep.km_mil);
        } else if (s_rep.km_clear >= 0) {
            snprintf(buf + n, sizeof(buf) - n, "  •  Silmeden beri %ld km",
                     (long)s_rep.km_clear);
        }
        break;
    }
    lv_label_set_text(s_status, buf);
}

static void refresh(bool connected)
{
    const ui_theme_t *t = theme_get();
    static dtc_entry_t s_built[DTC_MAX];
    static int s_built_n = -1;
    sort_codes();
    /* Tarama ilerlerken seq her adımda artar; kartları yalnız kodlar
     * değiştiğinde yeniden kur (kaydırma konumu da korunur). */
    if (s_built_n != s_sorted_n ||
        memcmp(s_built, s_sorted, sizeof(dtc_entry_t) * s_sorted_n) != 0) {
        memcpy(s_built, s_sorted, sizeof(dtc_entry_t) * s_sorted_n);
        s_built_n = s_sorted_n;
        build_list();
    }
    set_nova_color(report_color(t));
    refresh_core(t);
    refresh_status(connected);

    bool b = busy(s_rep.status);
    set_enabled(s_btn_scan, connected && !b);
    set_enabled(s_btn_clear, connected && !b &&
                (s_rep.count > 0 || (s_rep.monitor_valid && s_rep.mil_on)));
    set_scan_anim(b);
}

/* ---------- detay ---------- */

static void fmt_ff(char *buf, size_t len)
{
    const dtc_freeze_t *ff = &s_rep.ff;
    int n = snprintf(buf, len, "DONMUŞ VERİ (arıza anı)\n");
    if (!isnan(ff->rpm))   n += snprintf(buf + n, len - n, "Devir %.0f rpm   ", ff->rpm);
    if (!isnan(ff->speed)) n += snprintf(buf + n, len - n, "Hız %.0f %s",
                                         vehicle_data_convert_speed(ff->speed, s_metric),
                                         vehicle_data_speed_unit(s_metric));
    n += snprintf(buf + n, len - n, "\n");
    if (!isnan(ff->coolant)) n += snprintf(buf + n, len - n, "Su %.0f%s   ",
                                           vehicle_data_convert_temp(ff->coolant, s_metric),
                                           vehicle_data_temp_unit(s_metric));
    if (!isnan(ff->load))  n += snprintf(buf + n, len - n, "Yük %%%.0f   ", ff->load);
    if (!isnan(ff->map))   n += snprintf(buf + n, len - n, "MAP %.0f kPa", ff->map);
    if (!isnan(ff->stft) || !isnan(ff->ltft)) {
        snprintf(buf + n, len - n, "\nYakıt düzeltme K %+.0f%%  U %+.0f%%",
                 isnan(ff->stft) ? 0.0f : ff->stft, isnan(ff->ltft) ? 0.0f : ff->ltft);
    }
}

static void show_detail(int idx)
{
    const ui_theme_t *t = theme_get();
    if (idx < 0 || idx >= s_sorted_n) {
        return;
    }
    const dtc_entry_t *e = &s_sorted[idx];
    dtc_info_t info;
    dtc_db_lookup(e->code, &info);
    lv_color_t col = code_color(t, info.sev, e->kind);

    char code[6];
    obd_dtc_format(e->code, code);
    lv_label_set_text(s_det_code, code);
    lv_obj_set_style_text_color(s_det_code, col, 0);
    lv_obj_set_style_border_color(s_det_ring, col, 0);

    char meta[64];
    snprintf(meta, sizeof(meta), "%s  •  %s  •  %s%s", info.system, kind_text(e->kind),
             sev_text(info.sev), info.oem ? "  •  GM/Daewoo" : "");
    lv_label_set_text(s_det_meta, meta);
    lv_label_set_text(s_det_desc, info.desc);

    char hint[96];
    if (info.hint) {
        snprintf(hint, sizeof(hint), "Olası neden: %s", info.hint);
    } else {
        snprintf(hint, sizeof(hint), "%s", info.known ? "" : "Kod tabloda yok: grup açıklaması");
    }
    lv_label_set_text(s_det_hint, hint);

    /* Donmuş veri yalnız onu tetikleyen kod için anlamlı. */
    dtc_hist_t h;
    obd_dtc_get_history(&h);
    char ff[200] = "";
    int n = 0;
    if (s_rep.ff.valid && s_rep.ff.code == e->code) {
        fmt_ff(ff, sizeof(ff));
        n = (int)strlen(ff);
        n += snprintf(ff + n, sizeof(ff) - n, "\n");
    }
    for (int i = 0; i < h.count; i++) {
        if (h.items[i].code == e->code) {
            snprintf(ff + n, sizeof(ff) - n, "Kayıt: %u taramada görüldü, ilk #%u",
                     h.items[i].hits, h.items[i].first_scan);
            break;
        }
    }
    lv_label_set_text(s_det_ff, ff);

    lv_obj_clear_flag(s_detail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_detail);
}

static void close_overlay_cb(lv_event_t *e)
{
    lv_obj_add_flag((lv_obj_t *)lv_event_get_user_data(e), LV_OBJ_FLAG_HIDDEN);
}

/* ---------- geçmiş ---------- */

static void build_history(void)
{
    const ui_theme_t *t = theme_get();
    static dtc_hist_t h;
    obd_dtc_get_history(&h);

    char sum[80];
    if (h.clear_no) {
        snprintf(sum, sizeof(sum), "%u tarama  •  %u silme (son #%u)", h.scan_no, h.clear_no,
                 h.last_clear_scan);
    } else {
        snprintf(sum, sizeof(sum), "%u tarama  •  %u farklı kod", h.scan_no, h.count);
    }
    lv_label_set_text(s_hist_sum, sum);

    /* Son görülen en üstte. */
    uint8_t order[DTC_HIST_MAX];
    for (int i = 0; i < h.count; i++) order[i] = (uint8_t)i;
    for (int i = 1; i < h.count; i++) {
        uint8_t k = order[i];
        int j = i - 1;
        while (j >= 0 && h.items[order[j]].last_scan < h.items[k].last_scan) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = k;
    }

    lv_obj_clean(s_hist_list);
    lv_obj_scroll_to_y(s_hist_list, 0, LV_ANIM_OFF);
    if (h.count == 0) {
        lv_obj_t *l = mk_label(s_hist_list, t->font_tr_sm, t->text_dim);
        lv_label_set_text(l, "Kayıtlı arıza yok");
        return;
    }
    for (int i = 0; i < h.count; i++) {
        const dtc_hist_item_t *it = &h.items[order[i]];
        dtc_info_t info;
        dtc_db_lookup(it->code, &info);
        bool cleared = (it->kind & DTC_KIND_CLEARED) != 0;
        lv_color_t col = cleared ? t->text_dim : code_color(t, info.sev, it->kind);
        char code[6];
        obd_dtc_format(it->code, code);

        lv_obj_t *row = mk_plain(s_hist_list);
        lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
        lv_obj_set_size(row, LIST_W, 46);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, t->border, 0);

        lv_obj_t *lc = mk_label(row, t->font_md, col);
        lv_label_set_text(lc, code);
        lv_obj_align(lc, LV_ALIGN_TOP_LEFT, 6, 2);

        char right[40];
        snprintf(right, sizeof(right), "%u kez  •  #%u%s", it->hits, it->last_scan,
                 cleared ? "  •  silindi" : "");
        lv_obj_t *lr = mk_label(row, t->font_tr_sm, t->text_dim);
        lv_label_set_text(lr, right);
        lv_obj_align(lr, LV_ALIGN_TOP_RIGHT, -6, 4);

        lv_obj_t *ld = mk_label(row, t->font_tr_sm, cleared ? t->text_dim : t->text);
        lv_label_set_long_mode(ld, LV_LABEL_LONG_DOT);
        lv_obj_set_width(ld, LIST_W - 12);
        lv_label_set_text(ld, info.desc);
        lv_obj_align(ld, LV_ALIGN_TOP_LEFT, 6, 24);
    }
}

static void hist_btn_cb(lv_event_t *e)
{
    (void)e;
    build_history();
    lv_obj_clear_flag(s_hist, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_hist);
}

/* ---------- onay ---------- */

static void open_confirm(int mode)
{
    s_conf_mode = mode;
    if (mode == CONFIRM_CLEAR) {
        lv_label_set_text(s_conf_title, "Arıza kodları silinsin mi?");
        lv_label_set_text(s_conf_body,
                          "Kontak AÇIK, motor KAPALI olmalı.\n"
                          "Donmuş veri ve hazırlık monitörleri de sıfırlanır; "
                          "muayeneden hemen önce silmeyin.");
    } else {
        lv_label_set_text(s_conf_title, "Arıza kaydı silinsin mi?");
        lv_label_set_text(s_conf_body,
                          "Cihazda saklanan tüm arıza geçmişi kalıcı olarak silinir. "
                          "Araçtaki kodlar etkilenmez.");
    }
    lv_obj_clear_flag(s_confirm, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_confirm);
}

static void confirm_yes_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_confirm, LV_OBJ_FLAG_HIDDEN);
    if (s_conf_mode == CONFIRM_CLEAR) {
        obd_dtc_request_clear();
    } else {
        obd_dtc_erase_history();
        build_history();
    }
}

static void scan_btn_cb(lv_event_t *e)
{
    (void)e;
    obd_dtc_request_scan();
}

static void core_click_cb(lv_event_t *e)
{
    (void)e;
    if (!lv_obj_has_state(s_btn_scan, LV_STATE_DISABLED)) {
        obd_dtc_request_scan();
    }
}

static void clear_btn_cb(lv_event_t *e)
{
    (void)e;
    open_confirm(CONFIRM_CLEAR);
}

static void erase_btn_cb(lv_event_t *e)
{
    (void)e;
    open_confirm(CONFIRM_ERASE);
}

/* ---------- oluşturma ---------- */

static lv_obj_t *mk_center_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t col,
                                 lv_coord_t w, lv_coord_t y)
{
    lv_obj_t *l = mk_label(parent, font, col);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    return l;
}

static void create_detail(lv_obj_t *root, const ui_theme_t *t)
{
    s_detail = mk_overlay(root);

    s_det_ring = mk_plain(s_detail);
    lv_obj_set_size(s_det_ring, UI_VIEWPORT_SZ - 16, UI_VIEWPORT_SZ - 16);
    lv_obj_set_style_radius(s_det_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_det_ring, 2, 0);
    lv_obj_set_style_border_opa(s_det_ring, LV_OPA_50, 0);
    lv_obj_center(s_det_ring);

    s_det_code = mk_center_label(s_detail, t->font_xl, t->text, 300, 44);
    s_det_meta = mk_center_label(s_detail, t->font_tr_sm, t->text_dim, 360, 104);
    lv_label_set_long_mode(s_det_meta, LV_LABEL_LONG_DOT);
    s_det_desc = mk_center_label(s_detail, t->font_tr_md, t->text, 360, 136);
    s_det_hint = mk_center_label(s_detail, t->font_tr_sm, t->warn, 360, 200);
    s_det_ff   = mk_center_label(s_detail, t->font_tr_sm, t->text_dim, 380, 244);

    lv_obj_t *b = mk_button(s_detail, "KAPAT", t->primary, false, close_overlay_cb, s_detail);
    lv_obj_set_width(b, 120);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, BTN_Y);
}

static void create_history(lv_obj_t *root, const ui_theme_t *t)
{
    s_hist = mk_overlay(root);

    lv_obj_t *title = mk_center_label(s_hist, t->font_tr_md, t->text, 300, 40);
    lv_label_set_text(title, "ARIZA GEÇMİŞİ");
    s_hist_sum = mk_center_label(s_hist, t->font_tr_sm, t->text_dim, 340, 68);

    s_hist_list = lv_obj_create(s_hist);
    lv_obj_remove_style_all(s_hist_list);
    lv_obj_set_size(s_hist_list, LIST_W, 262);
    lv_obj_align(s_hist_list, LV_ALIGN_TOP_MID, 0, 96);
    lv_obj_set_flex_flow(s_hist_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_hist_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(s_hist_list, LV_DIR_VER);
    lv_obj_clear_flag(s_hist_list, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);

    lv_obj_t *bc = mk_button(s_hist, "KAPAT", t->primary, false, close_overlay_cb, s_hist);
    lv_obj_set_width(bc, 110);
    lv_obj_align(bc, LV_ALIGN_TOP_MID, -60, BTN_Y);

    lv_obj_t *be = mk_button(s_hist, "KAYDI SİL", t->crit, false, erase_btn_cb, NULL);
    lv_obj_set_width(be, 110);
    lv_obj_align(be, LV_ALIGN_TOP_MID, 60, BTN_Y);
}

static void create_confirm(lv_obj_t *root, const ui_theme_t *t)
{
    s_confirm = mk_overlay(root);
    lv_obj_set_style_bg_opa(s_confirm, LV_OPA_90, 0);

    lv_obj_t *card = mk_plain(s_confirm);
    lv_obj_set_size(card, 360, 230);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, t->surface, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, t->crit, 0);
    lv_obj_center(card);

    s_conf_title = mk_center_label(card, t->font_tr_md, t->text, 330, 18);
    s_conf_body  = mk_center_label(card, t->font_tr_sm, t->text_dim, 320, 56);

    lv_obj_t *bn = mk_button(card, "VAZGEÇ", t->primary, false, close_overlay_cb, s_confirm);
    lv_obj_set_width(bn, 120);
    lv_obj_align(bn, LV_ALIGN_BOTTOM_MID, -68, -16);

    lv_obj_t *by = mk_button(card, "SİL", t->crit, true, confirm_yes_cb, NULL);
    lv_obj_set_width(by, 120);
    lv_obj_align(by, LV_ALIGN_BOTTOM_MID, 68, -16);
}

void screen_dtc_create(lv_obj_t *parent)
{
    const ui_theme_t *t = theme_get();

    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_pad_row(parent, 0, 0);

    lv_obj_t *root = mk_plain(parent);
    lv_obj_set_size(root, UI_VIEWPORT_SZ, UI_VIEWPORT_SZ);
    lv_obj_add_flag(root, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(root, LV_ALIGN_TOP_LEFT, 0, 0);

    /* Nova: dıştan içe — ışınlar, haleler, şok halkaları, yay, çekirdek. */
    s_rays = mk_circle(root, RAYS_D);
    lv_obj_add_event_cb(s_rays, rays_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    s_halo[0] = mk_circle(root, HALO2_D);
    lv_obj_set_style_bg_opa(s_halo[0], LV_OPA_10, 0);
    s_halo[1] = mk_circle(root, HALO1_D);
    lv_obj_set_style_bg_opa(s_halo[1], LV_OPA_10, 0);

    for (int i = 0; i < 2; i++) {
        s_ring[i] = mk_circle(root, CORE_D);
        lv_obj_set_style_border_width(s_ring[i], 2, 0);
        lv_obj_add_flag(s_ring[i], LV_OBJ_FLAG_HIDDEN);
    }

    s_arc = lv_arc_create(root);
    lv_obj_set_size(s_arc, ARC_D, ARC_D);
    lv_obj_align(s_arc, LV_ALIGN_CENTER, 0, NOVA_Y);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(s_arc, 270);
    lv_arc_set_bg_angles(s_arc, 0, 360);
    lv_arc_set_range(s_arc, 0, 100);
    lv_obj_set_style_arc_width(s_arc, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, t->arc_bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_arc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_arc, 0, 0);

    s_core = mk_circle(root, CORE_D);
    lv_obj_add_flag(s_core, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(s_core, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_color(s_core, t->bg, 0);
    lv_obj_set_style_bg_grad_dir(s_core, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(s_core, 2, 0);
    lv_obj_add_event_cb(s_core, core_click_cb, LV_EVENT_CLICKED, NULL);

    s_core_num = mk_label(s_core, t->font_xl, t->text);
    lv_obj_align(s_core_num, LV_ALIGN_CENTER, 0, -10);
    s_core_cap = mk_label(s_core, t->font_tr_sm, t->text_dim);
    lv_obj_align(s_core_cap, LV_ALIGN_CENTER, 0, 24);   /* yuvarlak içinde ~85 px genişlik */

    s_status = mk_center_label(root, t->font_tr_sm, t->text_dim, 400, STATUS_Y);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_DOT);

    s_list = lv_obj_create(root);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_size(s_list, LIST_W, LIST_H);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, LIST_Y);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 6, 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_GESTURE_BUBBLE);

    s_empty = mk_center_label(root, t->font_tr_sm, t->text_dim, 300, LIST_Y + 40);
    lv_label_set_text(s_empty, LV_SYMBOL_OK "  Kayıtlı arıza kodu yok");

    s_btn_scan  = mk_button(root, "TARA", t->primary, true, scan_btn_cb, NULL);
    s_btn_clear = mk_button(root, "SİL", t->crit, false, clear_btn_cb, NULL);
    s_btn_hist  = mk_button(root, "GEÇMİŞ", t->text_dim, false, hist_btn_cb, NULL);
    lv_obj_align(s_btn_scan, LV_ALIGN_TOP_MID, -(BTN_W + 8), BTN_Y);
    lv_obj_align(s_btn_clear, LV_ALIGN_TOP_MID, 0, BTN_Y);
    lv_obj_align(s_btn_hist, LV_ALIGN_TOP_MID, BTN_W + 8, BTN_Y);

    create_detail(root, t);
    create_history(root, t);
    create_confirm(root, t);

    obd_dtc_get_report(&s_rep);
    s_seq = s_rep.seq;
    refresh(false);
    s_conn = 0;
}

void screen_dtc_update(const vehicle_data_snapshot_t *snap)
{
    bool connected = snap->state == OBD_STATE_READY;
    s_metric = snap->metric_units;
    uint32_t seq = obd_dtc_seq();
    if (seq == s_seq && (int8_t)connected == s_conn) {
        return;
    }
    obd_dtc_get_report(&s_rep);
    s_seq = s_rep.seq;
    s_conn = (int8_t)connected;
    refresh(connected);
}
