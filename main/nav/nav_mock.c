#include "nav_mock.h"
#include "nav_state.h"

#include <stdio.h>
#include "esp_timer.h"

/* Sahte rota: telefon entegrasyonu gelene kadar NAV ekranını beslemek için.
 * 1 sn'de bir sabit hızla ilerler; varıştan sonra baştan başlar. */

#define MOCK_TICK_MS     1000
#define MOCK_SPEED_KMH   54.0f
#define MOCK_ETA_BASE    (18 * 60 + 30)   /* 18:30 */
#define MOCK_ARRIVE_HOLD 5                /* varış ekranı kaç tik kalır */

typedef struct {
    nav_maneuver_t man;
    uint32_t       dist_m;     /* bu adımın başlangıcında manevraya mesafe */
    const char    *road;       /* şu an üzerinde olunan yol */
    const char    *next;       /* manevradan sonraki yol */
} mock_step_t;

static const mock_step_t s_route[] = {
    { NAV_MAN_RIGHT,        800,  "Ankara Caddesi",       "Atatürk Bulvarı" },
    { NAV_MAN_SLIGHT_LEFT,  450,  "Atatürk Bulvarı",      "Gazi Mustafa Kemal Blv." },
    { NAV_MAN_ROUNDABOUT,   1300, "Gazi Mustafa Kemal Blv.", "Kızılay Meydanı" },
    { NAV_MAN_LEFT,         600,  "Kızılay Meydanı",      "Ziya Gökalp Caddesi" },
    { NAV_MAN_STRAIGHT,     2400, "Ziya Gökalp Caddesi",  "Şıhhiye Köprüsü" },
    { NAV_MAN_SLIGHT_RIGHT, 350,  "Şıhhiye Köprüsü",      "Talatpaşa Bulvarı" },
    { NAV_MAN_UTURN,        200,  "Talatpaşa Bulvarı",    "Talatpaşa Bulvarı" },
    { NAV_MAN_ARRIVAL,      300,  "Talatpaşa Bulvarı",    "" },
};
#define ROUTE_LEN (sizeof(s_route) / sizeof(s_route[0]))

static esp_timer_handle_t s_timer;
static int      s_step;
static float    s_left_m;       /* mevcut adımda manevraya kalan */
static int      s_hold;

static uint32_t route_remaining(void)
{
    float r = s_left_m;
    for (int i = s_step + 1; i < (int)ROUTE_LEN; i++) {
        r += s_route[i].dist_m;
    }
    return (uint32_t)r;
}

static void publish(void)
{
    const mock_step_t *st = &s_route[s_step];
    uint32_t remain = route_remaining();
    float mps = MOCK_SPEED_KMH / 3.6f;

    nav_state_t *ns = nav_state_begin();
    ns->connected = true;
    ns->active = true;
    ns->maneuver = st->man;
    ns->maneuver_dist_m = (uint32_t)s_left_m;
    ns->remain_m = remain;
    ns->eta_min = (int16_t)((MOCK_ETA_BASE + (int)(remain / mps / 60.0f)) % (24 * 60));
    ns->speed_kmh = s_hold ? 0.0f : MOCK_SPEED_KMH;
    ns->heading = 90.0f;
    snprintf(ns->cur_road, sizeof(ns->cur_road), "%s", st->road);
    snprintf(ns->next_road, sizeof(ns->next_road), "%s", st->next);
    snprintf(ns->destination, sizeof(ns->destination), "%s", "Ulus");
    nav_state_commit();
}

static void restart_route(void)
{
    s_step = 0;
    s_left_m = (float)s_route[0].dist_m;
    s_hold = 0;
}

static void tick_cb(void *arg)
{
    (void)arg;
    if (s_hold) {
        if (--s_hold == 0) {
            restart_route();
        }
        publish();
        return;
    }

    s_left_m -= MOCK_SPEED_KMH / 3.6f * (MOCK_TICK_MS / 1000.0f);
    if (s_left_m <= 0.0f) {
        if (s_step + 1 < (int)ROUTE_LEN) {
            s_step++;
            s_left_m = (float)s_route[s_step].dist_m;
        } else {
            s_left_m = 0.0f;
            s_hold = MOCK_ARRIVE_HOLD;
        }
    }
    publish();
}

void nav_mock_start(void)
{
    if (!s_timer) {
        const esp_timer_create_args_t args = {
            .callback = tick_cb,
            .name = "nav_mock",
        };
        if (esp_timer_create(&args, &s_timer) != ESP_OK) {
            return;
        }
    }
    restart_route();
    publish();
    esp_timer_start_periodic(s_timer, MOCK_TICK_MS * 1000);
}

void nav_mock_stop(void)
{
    if (s_timer) {
        esp_timer_stop(s_timer);
    }
}
