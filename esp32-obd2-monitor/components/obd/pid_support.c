#include "pid_support.h"
#include <string.h>

#define PID_SUPPORT_MAX 0x100

static bool pid_bits[PID_SUPPORT_MAX];
static bool bitmap_known;

void pid_support_reset(void)
{
    memset(pid_bits, 0, sizeof(pid_bits));
    bitmap_known = false;
}

void pid_support_merge_block(uint8_t query_pid, const uint8_t *data, size_t data_len)
{
    if (data == NULL || data_len == 0) {
        return;
    }

    const unsigned base = (unsigned)query_pid + 1U;

    for (unsigned i = 0; i < data_len * 8U; i++) {
        const unsigned pid = base + i;
        if (pid >= PID_SUPPORT_MAX) {
            break;
        }
        const unsigned byte_idx = i / 8U;
        const unsigned bit = 7U - (i % 8U);
        if ((data[byte_idx] >> bit) & 1U) {
            pid_bits[pid] = true;
        }
    }

    bitmap_known = true;
}

bool pid_support_is_known(void)
{
    return bitmap_known;
}

bool pid_support_is_supported(uint8_t pid)
{
    (void)pid;
    if (!bitmap_known) {
        return true;
    }
    return pid_bits[pid];
}

bool pid_support_should_poll(uint8_t pid)
{
    if (!bitmap_known) {
        return true;
    }
    return pid_bits[pid];
}

void pid_support_mark_supported(uint8_t pid)
{
    pid_bits[pid] = true;
    bitmap_known = true;
}

void pid_support_mark_unsupported(uint8_t pid)
{
    pid_bits[pid] = false;
    bitmap_known = true;
}
