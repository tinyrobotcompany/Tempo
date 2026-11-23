#pragma once
#include "esp_err.h"
/* A persistent random bearer credential, never included in status or logs. */
esp_err_t station_pairing_load(char token[33]);

/* Revoke all existing phone credentials; caller publishes only on successful commit. */
esp_err_t station_pairing_rotate(char token[33]);
