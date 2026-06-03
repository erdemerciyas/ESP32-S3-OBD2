#include "pid_table.h"
#include <string.h>

const pid_info_t *pid_get_info(pid_type_t pid)
{
    for (int i = 0; i < PID_TABLE_SIZE; i++) {
        if (pid_table[i].pid == pid) {
            return &pid_table[i];
        }
    }
    return NULL;
}
