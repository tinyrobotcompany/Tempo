#pragma once
#include "esp_err.h"
#include "station_core.h"
esp_err_t station_settings_load(station_settings_t *settings);
esp_err_t station_settings_save(const station_settings_t *settings);
esp_err_t station_settings_worker_start(void);
bool station_settings_request_save(const station_settings_t *settings);
int station_settings_save_state(void); /* 0 idle, 1 pending, 2 saved, 3 failed */

/* Local mode (unencrypted phone access on port 80). Stored as its own key, outside
 * the settings blob, so no phone/settings/OTA payload can carry it. Missing = off.
 * Only phone_setup.c writes it, after a confirmed request on the display. */
typedef enum { STATION_LOCAL_MODE_OFF=0, STATION_LOCAL_MODE_ON=1, STATION_LOCAL_MODE_ROTATE=2 } station_local_mode_t;
/* ROTATE: switched off, but the pairing token sent over HTTP is not yet replaced. */
esp_err_t station_local_mode_load(station_local_mode_t *mode);
esp_err_t station_local_mode_save(station_local_mode_t mode);
