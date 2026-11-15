#pragma once
#include "station_core.h"
#include "esp_err.h"
esp_err_t station_network_start(void);
bool station_network_connect(const char *ssid, const char *password);
bool station_network_auto(bool enabled);
bool station_network_manual(time_t value);
void station_network_status(station_network_status_t *result);
bool station_network_location(const char *coordinates);

bool station_network_scan(void);
void station_network_scan_status(station_wifi_scan_t *result);

bool station_network_city(unsigned index);

bool station_network_home(const char *coordinates, const char *name);
