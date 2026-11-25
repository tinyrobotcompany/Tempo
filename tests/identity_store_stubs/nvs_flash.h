#pragma once
#include "platform.h"
typedef struct {unsigned char eky[32],tky[32];} nvs_sec_cfg_t;
int nvs_flash_secure_init_partition(const char *,nvs_sec_cfg_t *);
int nvs_open_from_partition(const char *,const char *,int,nvs_handle_t *);
int nvs_get_blob(nvs_handle_t,const char *,void *,size_t *);
int nvs_set_blob(nvs_handle_t,const char *,const void *,size_t);
