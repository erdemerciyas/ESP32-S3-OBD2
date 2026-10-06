#include "nav_state.h"

#include <string.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static nav_state_t s_state;
static SemaphoreHandle_t s_mutex;
static volatile uint32_t s_rev;

uint32_t nav_state_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void set_defaults(nav_state_t *st)
{
    uint32_t rev = st->rev;
    memset(st, 0, sizeof(*st));
    st->rev = rev;
    st->eta_min = -1;
    st->speed_kmh = -1.0f;
    st->heading = -1.0f;
}

void nav_state_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    set_defaults(&s_state);
}

nav_state_t *nav_state_begin(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    return &s_state;
}

void nav_state_commit(void)
{
    s_state.last_rx_ms = nav_state_now_ms();
    s_state.rev = ++s_rev;
    xSemaphoreGive(s_mutex);
}

void nav_state_reset(void)
{
    nav_state_t *st = nav_state_begin();
    set_defaults(st);
    nav_state_commit();
}

void nav_state_snapshot(nav_state_t *out)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_state;
    xSemaphoreGive(s_mutex);
}

uint32_t nav_state_rev(void)
{
    return s_rev;
}
