#include "nav_map.h"
#include "app_log.h"

#include <string.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "jpeg_decoder.h"

static const char *TAG = "nav_map";

#define DECODE_STACK 6144
#define DECODE_PRIO  2

static uint8_t  *s_jpg;              /* gelen JPEG (PSRAM) */
static uint16_t *s_buf[2];           /* çözülmüş RGB565: [s_front] UI'da */
static int       s_front;
static nav_map_hdr_t s_rx_hdr;       /* alınmakta olan */
static uint32_t  s_received;
static bool      s_receiving;
static nav_map_hdr_t s_dec_hdr;      /* çözülmekte olan */
static nav_map_hdr_t s_ready_hdr;    /* arka tamponda hazır */
static volatile bool s_decoding;
static volatile bool s_ready;
static SemaphoreHandle_t s_mutex;
static TaskHandle_t s_task;
static nav_map_result_cb_t s_result_cb;

static void report(uint8_t id, const char *why)
{
    if (s_result_cb) {
        s_result_cb(id, why);
    }
}

void nav_map_set_result_cb(nav_map_result_cb_t cb)
{
    s_result_cb = cb;
}

static bool ensure_buffers(void)
{
    if (s_jpg) {
        return true;
    }
    const size_t px = NAV_MAP_MAX_W * NAV_MAP_MAX_H * sizeof(uint16_t);
    s_jpg = heap_caps_malloc(NAV_MAP_MAX_LEN, MALLOC_CAP_SPIRAM);
    s_buf[0] = heap_caps_malloc(px, MALLOC_CAP_SPIRAM);
    s_buf[1] = heap_caps_malloc(px, MALLOC_CAP_SPIRAM);
    if (!s_jpg || !s_buf[0] || !s_buf[1]) {
        app_log_error(TAG, "No PSRAM for map buffers");
        heap_caps_free(s_jpg);
        heap_caps_free(s_buf[0]);
        heap_caps_free(s_buf[1]);
        s_jpg = NULL;
        s_buf[0] = s_buf[1] = NULL;
        return false;
    }
    return true;
}

static void decode_task(void *arg)
{
    (void)arg;
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        int back = 1 - s_front;
        esp_jpeg_image_cfg_t cfg = {
            .indata = s_jpg,
            .indata_size = s_dec_hdr.len,
            .outbuf = (uint8_t *)s_buf[back],
            .outbuf_size = NAV_MAP_MAX_W * NAV_MAP_MAX_H * sizeof(uint16_t),
            .out_format = JPEG_IMAGE_FORMAT_RGB565,
            .out_scale = JPEG_IMAGE_SCALE_0,
            .flags = { .swap_color_bytes = 0 },
        };
        esp_jpeg_image_output_t img = {0};
        uint32_t t0 = xTaskGetTickCount();
        esp_err_t err = esp_jpeg_decode(&cfg, &img);
        if (err == ESP_OK && img.width <= NAV_MAP_MAX_W && img.height <= NAV_MAP_MAX_H) {
            s_ready_hdr = s_dec_hdr;
            s_ready_hdr.w = img.width;
            s_ready_hdr.h = img.height;
            s_ready = true;
            app_log_info(TAG, "Map #%u %ux%u decoded in %lu ms (px0=%04x)", s_dec_hdr.id,
                         img.width, img.height,
                         (unsigned long)((xTaskGetTickCount() - t0) * portTICK_PERIOD_MS),
                         s_buf[back][0]);
            report(s_dec_hdr.id, NULL);
        } else {
            app_log_warn(TAG, "Map #%u decode failed: %s", s_dec_hdr.id, esp_err_to_name(err));
            report(s_dec_hdr.id, "decode");
        }
        xSemaphoreGive(s_mutex);
        s_decoding = false;
    }
}

void nav_map_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    xTaskCreate(decode_task, "nav_map", DECODE_STACK, NULL, DECODE_PRIO, &s_task);
}

void nav_map_begin(const nav_map_hdr_t *hdr)
{
    s_receiving = false;
    if (s_decoding) {
        report(hdr->id, "busy");   /* önceki resim hâlâ çözülüyor: telefon yeniler */
        return;
    }
    if (hdr->len == 0 || hdr->len > NAV_MAP_MAX_LEN ||
        hdr->w > NAV_MAP_MAX_W || hdr->h > NAV_MAP_MAX_H || !ensure_buffers()) {
        app_log_warn(TAG, "Map #%u rejected (%lu B, %ux%u)", hdr->id,
                     (unsigned long)hdr->len, hdr->w, hdr->h);
        report(hdr->id, "size");
        return;
    }
    s_rx_hdr = *hdr;
    s_received = 0;
    s_receiving = true;
}

void nav_map_chunk(const uint8_t *data, size_t len)
{
    if (!s_receiving || len < 4 || data[0] != s_rx_hdr.id) {
        return;
    }
    uint32_t off = data[1] | (data[2] << 8) | ((uint32_t)data[3] << 16);
    size_t n = len - 4;
    if (off != s_received || off + n > s_rx_hdr.len) {
        app_log_warn(TAG, "Map #%u chunk gap (off %lu, have %lu)", s_rx_hdr.id,
                     (unsigned long)off, (unsigned long)s_received);
        report(s_rx_hdr.id, "gap");
        s_receiving = false;
        return;
    }
    memcpy(s_jpg + off, data + 4, n);
    s_received += n;
    if (s_received == s_rx_hdr.len) {
        s_receiving = false;
        s_dec_hdr = s_rx_hdr;
        s_decoding = true;
        xTaskNotifyGive(s_task);
    }
}

bool nav_map_take(nav_map_img_t *out)
{
    if (!s_ready) {
        return false;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_front = 1 - s_front;
    s_ready = false;
    out->pixels = s_buf[s_front];
    out->w = s_ready_hdr.w;
    out->h = s_ready_hdr.h;
    out->zoom = s_ready_hdr.zoom;
    out->lat = s_ready_hdr.lat;
    out->lon = s_ready_hdr.lon;
    xSemaphoreGive(s_mutex);
    return true;
}
