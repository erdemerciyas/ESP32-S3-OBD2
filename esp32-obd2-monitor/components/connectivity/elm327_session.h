#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*elm327_send_fn)(const char *data, size_t len);
typedef int (*elm327_recv_fn)(char *buf, size_t max_len, int timeout_ms);

bool elm327_response_looks_valid(const char *resp, size_t len);
bool elm327_probe_adapter(elm327_send_fn send, elm327_recv_fn recv);
bool elm327_init_session(elm327_send_fn send, elm327_recv_fn recv);
bool elm327_probe_obd_ready(elm327_send_fn send, elm327_recv_fn recv);

#ifdef __cplusplus
}
#endif
