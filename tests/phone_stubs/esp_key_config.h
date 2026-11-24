#pragma once
#include <stdint.h>
#define ESP_KEY_SOURCE_PSA 2
typedef struct {int source;struct {uint32_t key_id;} psa;} esp_key_config_t;
