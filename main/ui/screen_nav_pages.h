#pragma once

#include "lvgl.h"
#include "nav_state.h"

/* NAV ekranının ek sayfaları (dokunarak geçilir): harita ve rota özeti.
 * force: sayfa yeni açıldı, her şeyi yeniden çiz. */
lv_obj_t *nav_map_page_create(lv_obj_t *parent);
void nav_map_page_update(const nav_state_t *ns, bool force);

lv_obj_t *nav_trip_page_create(lv_obj_t *parent);
void nav_trip_page_update(bool force);

/* Rehberlik sayfasının canlı arka planı (aurora + perspektif yol). */
enum { NAV_BG_IDLE = 0, NAV_BG_GUIDE, NAV_BG_STALE };
lv_obj_t *nav_bg_create(lv_obj_t *parent);
void nav_bg_set_visible(bool on);
void nav_bg_update(const nav_state_t *ns, int mode);

/* Ortak biçimlendirme: "18 dk" / "1 sa 05 dk" */
void nav_format_duration(uint32_t s, char *buf, size_t len);
