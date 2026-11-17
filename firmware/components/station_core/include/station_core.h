#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef enum { VIEW_CLOCK, VIEW_WEATHER, VIEW_TRANSPORT, VIEW_SETTINGS, VIEW_COUNT } station_view_t;
typedef struct {
    uint8_t brightness;
    uint8_t theme;
    bool rotate;
    uint16_t dwell_seconds;
    uint32_t custom_color;
    uint8_t clock_font;
    char user_name[49];
    uint8_t night_mode; /* 0 disabled, 1 dim, 2 screen off */
    uint8_t night_brightness;
    uint16_t night_start, night_end; /* legacy schedule, retained for migration */
    uint16_t dim_after_minutes, off_after_minutes; /* 0 = never; 1..1440 idle minutes */
} station_settings_t;
station_settings_t station_defaults(void);
bool station_settings_valid(const station_settings_t *s);
station_view_t station_next_view(station_view_t current);
bool station_rotation_due(const station_settings_t *s, station_view_t view,
                          uint64_t now_ms, uint64_t last_change_ms);
bool station_parse_local_time(const char *text, time_t *result);
uint8_t station_digit_segments(char digit);

/* Inverse of ESP_LV_ADAPTER_ROTATE_90 for the native 800 x 1280 panel. */
bool station_touch_to_landscape(uint16_t raw_x, uint16_t raw_y, uint16_t *x, uint16_t *y);


typedef enum { TIME_UNSET, TIME_MANUAL, TIME_NETWORK } station_time_source_t;
typedef struct {
    bool connected;
    bool connecting;
    unsigned connection_seconds,retry_seconds;
    bool automatic;
    station_time_source_t source;
    time_t last_sync;
    unsigned time_completion;
    bool time_success;
    char message[96];
    char timezone[64];
    char tz_rule[96];
    char location_message[96];
    bool location_valid;
    double latitude, longitude;
    unsigned location_generation;
    char location_name[64];
    unsigned location_completion;
    bool location_success;
} station_network_status_t;
bool station_wifi_credentials_valid(const char *ssid, const char *password);
const char *station_clock_status(const station_network_status_t *status);

const char *station_timezone_rule(const char *name);
bool station_parse_coordinates(const char *text, double *latitude, double *longitude);

#define STATION_MAX_NETWORKS 20
typedef struct { char ssid[33]; int8_t rssi; bool secured,saved; } station_wifi_ap_t;
typedef enum { WIFI_SCAN_IDLE, WIFI_SCAN_RUNNING, WIFI_SCAN_READY, WIFI_SCAN_FAILED } station_scan_state_t;
typedef struct {
    station_scan_state_t state;
    unsigned revision;
    unsigned count;
    station_wifi_ap_t networks[STATION_MAX_NETWORKS];
    char message[96];
} station_wifi_scan_t;
/* Keep strongest AP for each SSID and sort descending by signal strength. */
void station_scan_add(station_wifi_scan_t *scan, const station_wifi_ap_t *ap);

#define STATION_SETTINGS_V2_BYTES 59
#define STATION_SETTINGS_V3_BYTES 65
#define STATION_SETTINGS_BYTES 69
/* Idle policy uses monotonic time only. Dim level is perceptual, relative to normal. */
unsigned station_power_brightness(const station_settings_t *s, uint64_t now_ms, uint64_t last_activity_ms);
unsigned station_dim_brightness(const station_settings_t *s);
bool station_settings_decode(const uint8_t *data, unsigned size, station_settings_t *settings);
void station_settings_encode(const station_settings_t *settings, uint8_t data[STATION_SETTINGS_BYTES]);
typedef struct { const char *name, *zone, *coordinates; } station_city_t;
extern const station_city_t station_cities[];
extern const unsigned station_city_count;
