#pragma once
#include "esp_err.h"
#include "station_core.h"
esp_err_t station_settings_load(station_settings_t *settings);
esp_err_t station_settings_save(const station_settings_t *settings);
esp_err_t station_settings_worker_start(void);
bool station_settings_request_save(const station_settings_t *settings);
int station_settings_save_state(void); /* 0 idle, 1 pending, 2 saved, 3 failed */
