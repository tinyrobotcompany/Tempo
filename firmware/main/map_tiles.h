#pragma once
#include "station_map.h"
#include "esp_err.h"
esp_err_t station_map_tiles_start(void);
bool station_map_tile_acquire(unsigned zoom,int x,int y,station_map_tile_t *tile);
void station_map_tile_release(unsigned handle);
void station_map_cancel_pending(void);
