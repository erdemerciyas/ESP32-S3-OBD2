#include "app_log.h"
#include "esp_log.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define APP_LOG_LINES  48
#define APP_LOG_LINE_LEN 96

static char s_lines[APP_LOG_LINES][APP_LOG_LINE_LEN];
static int s_head;

static void push_line(const char *line)
{
    snprintf(s_lines[s_head], APP_LOG_LINE_LEN, "%s", line);
    s_head = (s_head + 1) % APP_LOG_LINES;
}

static void log_line(const char *level, const char *tag, const char *fmt, va_list args)
{
    char msg[APP_LOG_LINE_LEN - 16];
    vsnprintf(msg, sizeof(msg), fmt, args);

    char row[APP_LOG_LINE_LEN];
    snprintf(row, sizeof(row), "[%s][%s] %s", level, tag, msg);
    push_line(row);

    if (strcmp(level, "E") == 0) {
        ESP_LOGE(tag, "%s", msg);
    } else if (strcmp(level, "W") == 0) {
        ESP_LOGW(tag, "%s", msg);
    } else {
        ESP_LOGI(tag, "%s", msg);
    }
}

void app_log_info(const char *tag, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_line("I", tag, fmt, args);
    va_end(args);
}

void app_log_warn(const char *tag, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_line("W", tag, fmt, args);
    va_end(args);
}

void app_log_error(const char *tag, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_line("E", tag, fmt, args);
    va_end(args);
}
