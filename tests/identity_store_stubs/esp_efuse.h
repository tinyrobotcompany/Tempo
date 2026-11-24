#pragma once
#include <stdbool.h>
typedef int esp_efuse_block_t;
#define EFUSE_BLK_KEY0 4
#define ESP_EFUSE_KEY_PURPOSE_HMAC_UP 7
int esp_efuse_get_key_purpose(int);
bool esp_efuse_get_key_dis_read(int);
bool esp_efuse_get_key_dis_write(int);
bool esp_efuse_get_keypurpose_dis_write(int);
