#pragma once
#include "station_weather.h"
#include "esp_err.h"
esp_err_t station_weather_start(void);
void station_weather_status(station_weather_snapshot_t *result);
bool station_weather_refresh(void);
