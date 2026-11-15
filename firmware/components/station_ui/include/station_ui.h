#pragma once
#include "station_core.h"
#include "station_phone_types.h"
#include "lvgl.h"
#include "station_weather.h"
#include "station_transport.h"
#include "transport_lookup.h"
#include "station_map.h"
typedef struct {
    void (*transport_status)(station_transport_snapshot_t *status);
    bool (*transport_select)(unsigned index);
    bool (*transport_refresh)(void);
    bool (*transport_search)(const char *query);
    void (*transport_search_status)(station_transport_search_t *out);
    bool (*transport_search_select)(unsigned index);
    bool (*transport_line_map)(const char *line);
    void (*transport_line_map_status)(station_transport_line_map_t *out);
    bool (*transport_line_stop_select)(unsigned index);
    void (*transport_nearby_reset)(void);
    bool (*map_tile_acquire)(unsigned zoom,int x,int y,station_map_tile_t *out);
    void (*map_tile_release)(unsigned handle);
    void (*map_cancel_pending)(void);
    void (*phone_link)(station_phone_link_t *link);
    bool (*phone_take)(station_phone_command_t *command);
    void (*phone_publish)(const station_settings_t *settings, station_view_t view);
    void (*weather_status)(station_weather_snapshot_t *status);
    bool (*refresh_weather)(void);
    bool (*save)(const station_settings_t *settings);
    int (*save_state)(void);
    bool (*brightness)(unsigned percent);
    bool (*set_time)(time_t value);
    bool (*scan_wifi)(void);
    void (*wifi_scan_status)(station_wifi_scan_t *scan);
    bool (*connect_wifi)(const char *ssid, const char *password);
    bool (*set_location)(const char *coordinates);
    bool (*set_city)(unsigned index);
    bool (*automatic_time)(bool enabled);
    void (*network_status)(station_network_status_t *status);
} station_ui_actions_t;
void station_ui_start(const station_settings_t *settings, station_ui_actions_t actions);
unsigned station_ui_heartbeat(void);
/* Called by the LVGL touch reader. Consume the complete wake gesture. */
bool station_ui_touch(bool pressed);
