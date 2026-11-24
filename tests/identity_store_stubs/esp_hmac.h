#pragma once
#include <stddef.h>
typedef int hmac_key_id_t;
int esp_hmac_calculate(int,const void *,size_t,unsigned char *);
