#include "unity.h"
#include "obd_parser.h"
#include "pid_table.h"
#include <string.h>

TEST_CASE("parse RPM ascii response", "[obd]")
{
    const char *raw = "41 0C 1A F8\r>";
    uint8_t pid = 0;
    uint8_t bytes[8];
    size_t len = 0;

    TEST_ASSERT_EQUAL(ESP_OK, obd_parse_response_ascii((const uint8_t *)raw, strlen(raw), &pid, bytes, &len));
    TEST_ASSERT_EQUAL(0x0C, pid);
    TEST_ASSERT_EQUAL(2, len);

    const float rpm = obd_decode_pid_value(pid, bytes, len);
    TEST_ASSERT_UINT32_WITHIN(5, 1726, (uint32_t)rpm);
}

TEST_CASE("decode coolant PID", "[obd]")
{
    uint8_t bytes[] = {0x55};
    const float temp = obd_decode_pid_value(PID_COOLANT_TEMP, bytes, 1);
    TEST_ASSERT_INT32_WITHIN(1, 45, (int32_t)temp);
}
