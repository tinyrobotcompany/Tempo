#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Blocking resolver: call only from the main maintenance worker, never the UI. */
bool station_phone_dns_matches(const char *hostname,uint32_t station_address);
