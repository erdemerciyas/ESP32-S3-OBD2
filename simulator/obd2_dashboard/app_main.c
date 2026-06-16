#include "ui.h"
#include "demo_feed.h"
#include "vehicle_data.h"

void obd2_dashboard_app_init(void)
{
    vehicle_data_init();
    demo_feed_init();
    ui_init();
    ui_start_update_timer();
    demo_feed_start_connect();
}
