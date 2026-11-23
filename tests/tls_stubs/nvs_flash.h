#pragma once
#include "platform.h"
int nvs_flash_init_partition(const char *);
int nvs_open_from_partition(const char *,const char *,int,nvs_handle_t *);
