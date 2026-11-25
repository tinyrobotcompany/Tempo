#pragma once
#include "esp_err.h"
#include <stdint.h>
#define STATION_FACTORY_KEY_ID UINT32_C(0x30000001)
#define STATION_TLS_KEY_ID UINT32_C(0x30000002)
/* No erase, key export, eFuse programming or unencrypted fallback. */
esp_err_t station_identity_store_init(void);
