#pragma once

#include <lvgl.h>
#include "config.h"
#include <cstdint>

/** Phosphor / Workshop at Dusk — ui-demo.html parity */
namespace UiTheme {

constexpr uint32_t kLcdBg = 0x050605;
constexpr uint32_t kInk100 = 0xf4ebd9;
constexpr uint32_t kInk300 = 0xa89a7e;
constexpr uint32_t kInk400 = 0x6b6253;
constexpr uint32_t kInk500 = 0x3b362d;
constexpr uint32_t kInk700 = 0x18160f;

constexpr uint32_t kAmber100 = 0xffd99a;
constexpr uint32_t kAmber300 = 0xffb44a;
constexpr uint32_t kAmber500 = 0xf08a1c;
constexpr uint32_t kRust500 = 0xb14a2a;
constexpr uint32_t kCyan500 = 0x4ad6c2;
constexpr uint32_t kOlive500 = 0x87914a;

constexpr uint32_t kHairline = 0x3b362d;

/** Gauge dial (ticks, track, zones) — shared phosphor tuning */
constexpr uint32_t kGaugeTickMinor = 0x7a6e5c;
constexpr uint32_t kGaugeTickMajor = 0xd4c4a4;
constexpr uint32_t kGaugeTickLabel = 0xf4ebd9;
constexpr uint32_t kGaugeArcTrack = 0x322e28;
constexpr uint32_t kGaugeZoneLo = 0x2a4a38;
constexpr uint32_t kGaugeZoneMid = 0x4a3c24;
constexpr uint32_t kGaugeZoneHi = 0x5c2a20;

struct GaugeDialTheme {
    uint32_t tickMinor;
    uint32_t tickMajor;
    uint32_t tickLabel;
    uint32_t arcTrack;
    uint32_t accent;
    uint32_t valueText;
    uint32_t titleText;
    uint32_t unitText;
    uint32_t zoneLo;
    uint32_t zoneMid;
    uint32_t zoneHi;
    uint8_t zoneOpa;
};

uint32_t mixHex(uint32_t base, uint32_t blend, uint8_t blend_ratio);
uint32_t brightenHex(uint32_t hex, uint8_t amount);
GaugeDialTheme gaugeDialTheme(uint32_t accent_hex, bool show_zones);

constexpr int kRadiusCard = 12;
constexpr int kRadiusBtn = 8;
constexpr int kRadiusPill = 16;

lv_color_t color(uint32_t hex);
void applyScreen(lv_obj_t *screen);
void styleCard(lv_obj_t *obj, int radius = kRadiusCard);
void styleGlassPanel(lv_obj_t *obj);

lv_obj_t *makeRoundContent(lv_obj_t *parent);
lv_obj_t *makeStatusPill(lv_obj_t *parent, const char *text, bool on);
void setStatusPill(lv_obj_t *pill, bool on, bool alert = false);

lv_obj_t *makePrimaryBtn(lv_obj_t *parent, const char *text, int w, int h);
lv_obj_t *makeGhostBtn(lv_obj_t *parent, const char *text, int w, int h);
/** Larger hit target + short-click for Geri/Kapat style controls */
void styleNavBtn(lv_obj_t *btn);
void registerNavBtnEvents(lv_obj_t *btn, lv_event_cb_t cb, void *user_data);
void styleList(lv_obj_t *list);
void styleListBtn(lv_obj_t *btn);
void styleTextarea(lv_obj_t *ta);
void styleKeyboard(lv_obj_t *kb);
/** Compact keyboard for round 480x480 WiFi password entry */
void styleWifiKeyboard(lv_obj_t *kb);

lv_obj_t *makeIndicatorDot(lv_obj_t *parent, int index);
void setIndicatorActive(lv_obj_t *const *dots, int count, int active);

} // namespace UiTheme
