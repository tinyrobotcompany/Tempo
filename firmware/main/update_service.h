#pragma once
#include "esp_err.h"
#include "station_update.h"
esp_err_t station_update_start(void);
void station_update_status(station_update_status_t *out);
bool station_update_request(const char *action);
bool station_update_configure(unsigned hours,bool automatic,unsigned start,unsigned end);
bool station_update_validate_trial(void);

/* Called by the OTA worker before flash writes and after a failed install. */
void station_update_display_guard(bool (*guard)(bool quiet));
