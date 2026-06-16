#pragma once

void app_log_info(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void app_log_warn(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void app_log_error(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
