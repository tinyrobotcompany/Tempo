#pragma once
#include "station_phone_types.h"
#include "cJSON.h"
bool station_phone_json_shallow(const char *text);
/* Supply current/default settings; omitted night fields are preserved.
 * The destination remains unchanged on any validation failure. */
bool station_phone_parse_settings(const cJSON *json, station_settings_t *settings);
bool station_phone_parse_home(const cJSON *json, char coordinates[64], char name[64]);
