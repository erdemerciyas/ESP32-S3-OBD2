#include "unity.h"
#include "pid_support.h"

TEST_CASE("merge 0100 block marks RPM supported", "[obd]")
{
    pid_support_reset();
    const uint8_t data[] = {0xBE, 0x1F, 0xA8, 0x13};
    pid_support_merge_block(0x00, data, sizeof(data));

    TEST_ASSERT_TRUE(pid_support_is_known());
    TEST_ASSERT_TRUE(pid_support_is_supported(0x0C));
    TEST_ASSERT_FALSE(pid_support_is_supported(0x42));
}

TEST_CASE("merge 0140 block marks voltage supported", "[obd]")
{
    pid_support_reset();
    const uint8_t data[] = {0x40, 0x00, 0x00, 0x00};
    pid_support_merge_block(0x40, data, sizeof(data));

    TEST_ASSERT_TRUE(pid_support_is_supported(0x42));
}
