#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#define STATION_PRIM_KEY_MAX 512
/* Secrets never belong in snapshots, logs, settings exports, or firmware images. */
bool station_credential_configured(void);
bool station_credential_get(char *out, size_t size);
esp_err_t station_credential_set(const char *key);
