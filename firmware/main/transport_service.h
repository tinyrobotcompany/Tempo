#pragma once
#include "station_transport.h"
#include "esp_err.h"
esp_err_t station_transport_start(void);
void station_transport_status(station_transport_snapshot_t *out);
bool station_transport_select(unsigned index);
bool station_transport_refresh(void);

#include "transport_lookup.h"
bool station_transport_search(const char *query);
void station_transport_search_status(station_transport_search_t *out);
bool station_transport_search_select(unsigned index);
bool station_transport_line_map(const char *line);
void station_transport_line_map_status(station_transport_line_map_t *out);
bool station_transport_line_stop_select(unsigned index);
void station_transport_nearby_reset(void);
