#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#define WEATHER_HOURS 12
#define WEATHER_DAYS 3
typedef struct { time_t at; float temperature, rain_chance; int code; } station_weather_hour_t;
typedef struct { time_t at; float high, low, rain_chance, precipitation; int code; } station_weather_day_t;
typedef struct {
    bool valid, is_day;
    time_t observed_at, fetched_at;
    int utc_offset, interval_seconds;
    float temperature, feels_like, humidity, wind, gusts, direction, pressure, precipitation;
    int code;
    unsigned hour_count, day_count;
    station_weather_hour_t hours[WEATHER_HOURS];
    station_weather_day_t days[WEATHER_DAYS];
} station_weather_data_t;
typedef enum { WEATHER_LOCATION, WEATHER_WIFI, WEATHER_TIME, WEATHER_LOADING, WEATHER_READY, WEATHER_RETRY } station_weather_state_t;
typedef struct {
    station_weather_data_t data;
    station_weather_state_t state;
    unsigned revision, location_generation;
    bool stale;
    char location[64];
} station_weather_snapshot_t;
/* Pure, bounded provider adapter. Missing/null/wrong-unit values remain NAN. */
bool station_weather_parse(const char *json, time_t fetched_at, station_weather_data_t *result);
bool station_weather_url(char *url, size_t size, double latitude, double longitude);
const char *station_weather_condition(int code, bool is_day);
const char *station_weather_direction(float degrees);
bool station_weather_stale(const station_weather_data_t *data, time_t now);

int station_weather_day_index(const station_weather_data_t *data,time_t now);

/* Chart data uses real timestamp spacing; absent measurements stay NAN. */
typedef struct {
    unsigned count;
    time_t start, end;
    float low, high;
    station_weather_hour_t points[WEATHER_HOURS];
} station_weather_plot_t;
void station_weather_plot(const station_weather_data_t *data,time_t now,unsigned hours,station_weather_plot_t *plot);
bool station_weather_plot_join(const station_weather_plot_t *plot,unsigned right);
