#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "app.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t settings_load(app_settings_t *settings);
esp_err_t settings_save(const app_settings_t *settings);
void settings_reset(void);

#ifdef __cplusplus
}
#endif
