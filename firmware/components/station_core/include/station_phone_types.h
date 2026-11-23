#pragma once
#include "station_core.h"
typedef struct { bool change_settings, change_view; station_settings_t settings; station_view_t view; } station_phone_command_t;
typedef struct { bool online; int forget_state; char url[160]; } station_phone_link_t;
