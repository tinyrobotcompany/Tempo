#pragma once
/* Large route/connection snapshots must not consume internal Wi-Fi/TLS RAM. */
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#include "esp_attr.h"
#if !CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#error "Transport snapshots require external PSRAM BSS support"
#endif
#define STATION_TRANSPORT_DATA EXT_RAM_BSS_ATTR
#else
#define STATION_TRANSPORT_DATA
#endif
