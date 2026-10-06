#include "nav_track.h"

#include <math.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TRACK_CAP      16384    /* 128 KB PSRAM; dolunca yarıya seyreltilir */
#define TRACK_MIN_STEP 8.0f     /* m — GPS titremesini kaydetme */

static nav_track_pt_t *s_pts;
static size_t   s_count;
static uint32_t s_stride = 1;   /* seyreltme sonrası kaç örnekte bir nokta */
static uint32_t s_skip;
static float    s_dist_m;
static double   s_last_lat, s_last_lon;
static bool     s_has_last;
static uint32_t s_start_ms;
static uint32_t s_last_ms;
static char     s_roads[NAV_TRACK_MAX_ROADS][NAV_TEXT_LEN];
static int      s_road_count;
static volatile uint32_t s_rev;
static SemaphoreHandle_t s_mutex;

static float haversine_m(double lat1, double lon1, double lat2, double lon2)
{
    const double R = 6371000.0;
    double dlat = (lat2 - lat1) * M_PI / 180.0;
    double dlon = (lon2 - lon1) * M_PI / 180.0;
    double a = sin(dlat / 2) * sin(dlat / 2) +
               cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0) * sin(dlon / 2) * sin(dlon / 2);
    return (float)(2 * R * atan2(sqrt(a), sqrt(1 - a)));
}

void nav_track_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    s_pts = heap_caps_malloc(TRACK_CAP * sizeof(nav_track_pt_t), MALLOC_CAP_SPIRAM);
}

void nav_track_reset(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_count = 0;
    s_stride = 1;
    s_skip = 0;
    s_dist_m = 0;
    s_has_last = false;
    s_start_ms = 0;
    s_last_ms = 0;
    s_road_count = 0;
    s_rev++;
    xSemaphoreGive(s_mutex);
}

void nav_track_add(double lat, double lon)
{
    if (!s_pts || (lat == 0.0 && lon == 0.0)) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_has_last) {
        float d = haversine_m(s_last_lat, s_last_lon, lat, lon);
        if (d < TRACK_MIN_STEP) {
            xSemaphoreGive(s_mutex);
            return;
        }
        s_dist_m += d;
    } else {
        s_start_ms = nav_state_now_ms();
    }
    s_last_lat = lat;
    s_last_lon = lon;
    s_has_last = true;
    s_last_ms = nav_state_now_ms();

    if (++s_skip >= s_stride) {
        s_skip = 0;
        if (s_count == TRACK_CAP) {
            /* Dolu: her ikinci noktayı at, bundan sonra iki kat seyrek kaydet. */
            for (size_t i = 0; i < TRACK_CAP / 2; i++) {
                s_pts[i] = s_pts[i * 2];
            }
            s_count = TRACK_CAP / 2;
            s_stride *= 2;
        }
        s_pts[s_count].lat_e6 = (int32_t)lround(lat * 1e6);
        s_pts[s_count].lon_e6 = (int32_t)lround(lon * 1e6);
        s_count++;
    }
    s_rev++;
    xSemaphoreGive(s_mutex);
}

void nav_track_add_road(const char *road)
{
    if (!road || !road[0]) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_road_count == 0 || strcmp(s_roads[s_road_count - 1], road) != 0) {
        if (s_road_count == NAV_TRACK_MAX_ROADS) {
            memmove(s_roads[0], s_roads[1], (NAV_TRACK_MAX_ROADS - 1) * NAV_TEXT_LEN);
            s_road_count--;
        }
        strncpy(s_roads[s_road_count], road, NAV_TEXT_LEN - 1);
        s_roads[s_road_count][NAV_TEXT_LEN - 1] = '\0';
        s_road_count++;
        s_rev++;
    }
    xSemaphoreGive(s_mutex);
}

uint32_t nav_track_rev(void)
{
    return s_rev;
}

float nav_track_distance_m(void)
{
    return s_dist_m;
}

uint32_t nav_track_duration_s(void)
{
    return s_has_last ? (s_last_ms - s_start_ms) / 1000 : 0;
}

size_t nav_track_copy(nav_track_pt_t *out, size_t max)
{
    if (!s_pts || max == 0) {
        return 0;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    size_t n = 0;
    if (s_count <= max) {
        memcpy(out, s_pts, s_count * sizeof(*out));
        n = s_count;
    } else {
        for (size_t i = 0; i < max - 1; i++) {
            out[n++] = s_pts[i * s_count / (max - 1)];
        }
        out[n++] = s_pts[s_count - 1];
    }
    xSemaphoreGive(s_mutex);
    return n;
}

int nav_track_roads(char out[][NAV_TEXT_LEN], int max)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int n = s_road_count < max ? s_road_count : max;
    /* en yenileri: listenin sonundan */
    memcpy(out, s_roads[s_road_count - n], (size_t)n * NAV_TEXT_LEN);
    xSemaphoreGive(s_mutex);
    return n;
}
