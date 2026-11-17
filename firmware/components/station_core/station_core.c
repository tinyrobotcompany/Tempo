#include "station_core.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

station_settings_t station_defaults(void)
{
    return (station_settings_t){.brightness = 65, .theme = 0, .rotate = false, .dwell_seconds = 20, .custom_color = 0xd68cff, .night_brightness=10, .night_start=23*60, .night_end=7*60};
}
bool station_settings_valid(const station_settings_t *s)
{
    return s && s->brightness >= 5 && s->brightness <= 100 && s->theme < 4 && s->clock_font < 10 && s->custom_color <= 0xffffff &&
        memchr(s->user_name, 0, sizeof(s->user_name)) &&
        s->dwell_seconds >= 10 && s->dwell_seconds <= 300 &&
        s->dim_after_minutes <= 1440 && s->off_after_minutes <= 1440 &&
        (!s->dim_after_minutes || !s->off_after_minutes || s->off_after_minutes > s->dim_after_minutes) &&
        s->night_brightness >= 1 && s->night_brightness <= 100 && s->night_mode <= 2 && s->night_start < 1440 && s->night_end < 1440 &&
        (!s->night_mode || (s->night_start != s->night_end && s->night_brightness >= 1 && s->night_brightness <= 100));
}
station_view_t station_next_view(station_view_t current)
{
    return current >= VIEW_CLOCK && current < VIEW_TRANSPORT ? current + 1 : VIEW_CLOCK;
}
bool station_rotation_due(const station_settings_t *s, station_view_t view,
                          uint64_t now_ms, uint64_t last_change_ms)
{
    return station_settings_valid(s) && s->rotate && view != VIEW_SETTINGS &&
        now_ms >= last_change_ms && now_ms - last_change_ms >= (uint64_t)s->dwell_seconds * 1000;
}
/* Accept exact local civil time; reject impossible dates and DST gaps. */
bool station_parse_local_time(const char *text, time_t *result)
{
    if (!text || !result || strlen(text) != 16) return false;
    for (int i = 0; i < 16; ++i) {
        char separator = i == 4 || i == 7 ? '-' : i == 10 ? ' ' : i == 13 ? ':' : 0;
        if (separator ? text[i] != separator : !isdigit((unsigned char)text[i])) return false;
    }
    int y, m, d, h, min;
    if (sscanf(text, "%4d-%2d-%2d %2d:%2d", &y, &m, &d, &h, &min) != 5 ||
        y < 2024 || y > 2099 || m < 1 || m > 12 || d < 1 || d > 31 || h > 23 || min > 59) return false;
    struct tm requested = {.tm_year=y-1900, .tm_mon=m-1, .tm_mday=d,
                           .tm_hour=h, .tm_min=min, .tm_isdst=-1};
    time_t value = mktime(&requested);
    struct tm actual;
    if (value == (time_t)-1 || !localtime_r(&value, &actual)) return false;
    if (actual.tm_year != y-1900 || actual.tm_mon != m-1 || actual.tm_mday != d ||
        actual.tm_hour != h || actual.tm_min != min) return false;
    *result = value;
    return true;
}
uint8_t station_digit_segments(char digit)
{
    static const uint8_t segments[] = {0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
    return digit >= '0' && digit <= '9' ? segments[digit-'0'] : 0x40;
}

bool station_touch_to_landscape(uint16_t raw_x, uint16_t raw_y, uint16_t *x, uint16_t *y)
{
    if (!x || !y || raw_x >= 800 || raw_y >= 1280) return false;
    *x = raw_y;
    *y = 799 - raw_x;
    return true;
}

bool station_wifi_credentials_valid(const char *ssid, const char *password)
{
    if (!ssid || !password) return false;
    size_t name_len = strlen(ssid), pass_len = strlen(password);
    return name_len > 0 && name_len <= 32 && (pass_len == 0 || (pass_len >= 8 && pass_len <= 63));
}
const char *station_clock_status(const station_network_status_t *s)
{
    if (s->source == TIME_UNSET)
        return s->automatic ? "Waiting for internet time" : "Clock not set / Manual mode";
    if (!s->automatic) return "Manual time";
    if (!s->connected) return "Offline - clock continuing";
    if (s->source == TIME_MANUAL) return "Manual time - waiting for sync";
    return "Internet time";
}

bool station_parse_coordinates(const char *text, double *latitude, double *longitude)
{
    if (!text || !latitude || !longitude) return false;
    double lat, lon; char tail;
    if (sscanf(text, " %lf , %lf %c", &lat, &lon, &tail) != 2 || !isfinite(lat) || !isfinite(lon) ||
        lat < -90 || lat > 90 || lon < -180 || lon > 180) return false;
    *latitude = lat; *longitude = lon;
    return true;
}

void station_scan_add(station_wifi_scan_t *scan, const station_wifi_ap_t *ap)
{
    if (!scan || !ap || !ap->ssid[0] || !memchr(ap->ssid, 0, sizeof(ap->ssid))) return;
    unsigned index = scan->count;
    for (unsigned i = 0; i < scan->count; ++i) {
        if (strcmp(scan->networks[i].ssid, ap->ssid) == 0) {
            if (scan->networks[i].rssi >= ap->rssi) return;
            index = i; break;
        }
    }
    if (index == scan->count) {
        if (scan->count < STATION_MAX_NETWORKS) scan->count++;
        else {
            index = STATION_MAX_NETWORKS - 1;
            if (scan->networks[index].rssi >= ap->rssi) return;
        }
    }
    scan->networks[index] = *ap;
    while (index > 0 && scan->networks[index].rssi > scan->networks[index-1].rssi) {
        station_wifi_ap_t previous = scan->networks[index-1];
        scan->networks[index-1] = scan->networks[index];
        scan->networks[index] = previous;
        --index;
    }
}

unsigned station_dim_brightness(const station_settings_t *s)
{
    /* Quadratic approximation to perceived brightness; the BSP accepts integer PWM percent.
     * Preserve a visible minimum for dim; only screen-off requests zero. */
    unsigned level = s->brightness * s->night_brightness * s->night_brightness / 10000;
    return level ? level : 1;
}
unsigned station_power_brightness(const station_settings_t *s, uint64_t now_ms, uint64_t last_activity_ms)
{
    uint64_t idle = now_ms >= last_activity_ms ? now_ms - last_activity_ms : 0;
    if (s->off_after_minutes && idle >= (uint64_t)s->off_after_minutes * 60000) return 0;
    if (s->dim_after_minutes && idle >= (uint64_t)s->dim_after_minutes * 60000) return station_dim_brightness(s);
    return s->brightness;
}

/* Version 1 migrates without discarding brightness, theme or rotation. */
bool station_settings_decode(const uint8_t *d, unsigned n, station_settings_t *out)
{
    if (!d || !out || !((n==6 && d[0]==1) || (n==STATION_SETTINGS_V2_BYTES && d[0]==2) || (n==STATION_SETTINGS_V3_BYTES && d[0]==3) || (n==STATION_SETTINGS_BYTES && d[0]==4)) || d[3]>1) return false;
    station_settings_t s=station_defaults();
    s.brightness=d[1]; s.theme=d[2]; s.rotate=d[3]; s.dwell_seconds=d[4]|(d[5]<<8);
    if(d[0]>=2) { s.custom_color=((uint32_t)d[6]<<16)|((uint32_t)d[7]<<8)|d[8]; s.clock_font=d[9]; memcpy(s.user_name,d+10,49); }
    if(d[0]>=3) { s.night_mode=d[59]; s.night_brightness=d[60]; s.night_start=d[61]|(d[62]<<8); s.night_end=d[63]|(d[64]<<8); }
    if(d[0]==3) { s.dim_after_minutes=s.night_mode==1?1:0; s.off_after_minutes=s.night_mode==2?1:0; }
    if(d[0]==4) { s.dim_after_minutes=d[65]|(d[66]<<8); s.off_after_minutes=d[67]|(d[68]<<8); }
    if(!station_settings_valid(&s)) return false;
    *out=s; return true;
}
void station_settings_encode(const station_settings_t *s, uint8_t d[STATION_SETTINGS_BYTES])
{
    memset(d,0,STATION_SETTINGS_BYTES); d[0]=4; d[1]=s->brightness; d[2]=s->theme; d[3]=s->rotate;
    d[4]=s->dwell_seconds&255; d[5]=s->dwell_seconds>>8;
    d[6]=s->custom_color>>16; d[7]=s->custom_color>>8; d[8]=s->custom_color; d[9]=s->clock_font;
    memcpy(d+10,s->user_name,49);
    d[65]=s->dim_after_minutes&255; d[66]=s->dim_after_minutes>>8;
    d[67]=s->off_after_minutes&255; d[68]=s->off_after_minutes>>8;
    d[59]=s->night_mode; d[60]=s->night_brightness;
    d[61]=s->night_start&255; d[62]=s->night_start>>8; d[63]=s->night_end&255; d[64]=s->night_end>>8;
}
#include "cities.inc"
