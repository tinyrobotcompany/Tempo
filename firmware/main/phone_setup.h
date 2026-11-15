#pragma once
#include "esp_err.h"
#include "station_phone_types.h"
esp_err_t station_phone_start(const station_settings_t *settings);
void station_phone_link(station_phone_link_t *link);
bool station_phone_take(station_phone_command_t *command);
void station_phone_publish(const station_settings_t *settings, station_view_t view);
