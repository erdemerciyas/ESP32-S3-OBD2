#include "obd_dtc.h"
#include "demo_feed.h"

void obd_dtc_init(void)
{
}

void obd_dtc_read_active(void)
{
    demo_feed_load_dtcs();
}

void obd_dtc_read_pending(void)
{
}

void obd_dtc_clear(void)
{
    demo_feed_clear_dtcs();
}
