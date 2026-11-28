#pragma once
#include "esp_err.h"
#include "station_phone_types.h"
#include "station_local_mode.h"
esp_err_t station_phone_start(const station_settings_t *settings);
void station_phone_link(station_phone_link_t *link);
bool station_phone_take(station_phone_command_t *command);
void station_phone_publish(const station_settings_t *settings, station_view_t view);

void station_phone_forget(void);
void station_phone_maintenance(void);

/* Display only: request is STATION_LOCAL_QUERY/REQUEST_ON/REQUEST_OFF. Never blocks;
   station_phone_maintenance() applies it. Returns STATION_LOCAL_* status flags. */
unsigned station_phone_local_mode(int request);
