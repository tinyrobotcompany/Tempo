#pragma once
#include "esp_err.h"
/* A persistent random bearer credential, never included in status or logs. */
esp_err_t station_pairing_load(char token[33]);
