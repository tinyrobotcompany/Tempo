#include "station_core.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void)
{
    station_settings_t s = station_defaults();
    assert(station_settings_valid(&s));
    uint8_t legacy[]={1,42,2,1,30,0}; station_settings_t migrated;
    assert(station_settings_decode(legacy,sizeof(legacy),&migrated));
    assert(migrated.brightness==42 && migrated.theme==2 && migrated.rotate && migrated.dwell_seconds==30 && migrated.clock_font==0);
    migrated.theme=3; migrated.custom_color=0xabcdef; migrated.clock_font=1; strcpy(migrated.user_name,"Simon & Alex");
    uint8_t encoded[STATION_SETTINGS_BYTES]; station_settings_encode(&migrated,encoded);
    station_settings_t decoded; assert(station_settings_decode(encoded,sizeof(encoded),&decoded));
    assert(decoded.custom_color==0xabcdef && decoded.clock_font==1 && !strcmp(decoded.user_name,"Simon & Alex"));
    migrated.clock_font=3;station_settings_encode(&migrated,encoded);assert(station_settings_decode(encoded,sizeof(encoded),&decoded)&&decoded.clock_font==3);
    encoded[58]='x'; memset(encoded+10,'x',49); assert(!station_settings_decode(encoded,sizeof(encoded),&decoded));
    for(unsigned i=0;i<station_city_count;i++) { double lat,lon; assert(station_timezone_rule(station_cities[i].zone)); assert(station_parse_coordinates(station_cities[i].coordinates,&lat,&lon)); }
    s.brightness = 0; assert(!station_settings_valid(&s));
    s = station_defaults(); s.clock_font=3; assert(station_settings_valid(&s)); s.clock_font=9; assert(station_settings_valid(&s)); s.clock_font=10; assert(!station_settings_valid(&s));
    s = station_defaults(); s.rotate = true;
    assert(!station_rotation_due(&s, VIEW_CLOCK, 19999, 0));
    assert(station_rotation_due(&s, VIEW_CLOCK, 20000, 0));
    assert(!station_rotation_due(&s, VIEW_SETTINGS, 99999, 0));
    assert(!station_rotation_due(&s, VIEW_CLOCK, 0, 1000));
    assert(station_next_view(VIEW_TRANSPORT) == VIEW_CLOCK);
    assert(station_next_view(VIEW_CLOCK) == VIEW_WEATHER);
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); tzset();
    time_t t;
    assert(station_parse_local_time("2028-02-29 23:59", &t));
    assert(!station_parse_local_time("2026-02-29 12:00", &t));
    assert(!station_parse_local_time("2026-11-12 24:00", &t));
    assert(!station_parse_local_time("2026-03-29 02:30", &t)); /* Paris DST gap */
    assert(station_parse_local_time("2026-10-25 02:30", &t)); /* repeated hour */
    assert(!station_parse_local_time("2026-11-12 12:34x", &t));
    assert(!station_parse_local_time("2026/11/12 12:34", &t));
    assert(!station_parse_local_time(NULL, &t));
    assert(station_digit_segments('8') == 0x7f);
    assert(station_digit_segments('-') == 0x40);
    uint16_t x, y;
    assert(station_touch_to_landscape(0, 0, &x, &y) && x == 0 && y == 799);
    assert(station_touch_to_landscape(799, 1279, &x, &y) && x == 1279 && y == 0);
    assert(station_touch_to_landscape(60, 1105, &x, &y) && x == 1105 && y == 739);
    assert(!station_touch_to_landscape(800, 0, &x, &y));
    assert(!station_touch_to_landscape(0, 1280, &x, &y));
    assert(station_wifi_credentials_valid("Home", "12345678"));
    assert(station_wifi_credentials_valid("Open", ""));
    assert(!station_wifi_credentials_valid("", "12345678"));
    assert(!station_wifi_credentials_valid("Home", "short"));
    double lat,lon;
    assert(station_parse_coordinates("48.8566,2.3522", &lat, &lon));
    assert(!station_parse_coordinates("nan,2", &lat, &lon));
    assert(!station_parse_coordinates("91,0", &lat, &lon));
    assert(!station_parse_coordinates("0,0x", &lat, &lon));
    assert(station_timezone_rule("Europe/Paris"));
    assert(station_timezone_rule("America/New_York"));
    assert(station_timezone_rule("Asia/Tokyo"));
    assert(!station_timezone_rule("Unknown/Zone"));
    setenv("TZ", station_timezone_rule("America/New_York"), 1); tzset();
    assert(!station_parse_local_time("2026-03-08 02:30", &t));
    setenv("TZ", station_timezone_rule("Asia/Tokyo"), 1); tzset();
    assert(station_parse_local_time("2026-03-08 02:30", &t));
    station_network_status_t n={.automatic=true};
    assert(strcmp(station_clock_status(&n), "Waiting for internet time")==0);
    n.source=TIME_NETWORK;
    assert(strcmp(station_clock_status(&n), "Offline - clock continuing")==0);
    n.connected=true;
    assert(strcmp(station_clock_status(&n), "Internet time")==0);
    station_wifi_scan_t scan = {0};
    station_wifi_ap_t ap = {.ssid="Home", .rssi=-70, .secured=true};
    station_scan_add(&scan,&ap);
    ap.rssi=-40; station_scan_add(&scan,&ap);
    assert(scan.count==1 && scan.networks[0].rssi==-40);
    snprintf(ap.ssid,sizeof(ap.ssid),"Guest"); ap.rssi=-20;
    station_scan_add(&scan,&ap);
    assert(scan.count==2 && strcmp(scan.networks[0].ssid,"Guest")==0);
    ap.ssid[0]=0; station_scan_add(&scan,&ap); assert(scan.count==2);
    for (unsigned i=0; i<30; ++i) {
        snprintf(ap.ssid,sizeof(ap.ssid),"Network%u",i); ap.rssi=-60;
        station_scan_add(&scan,&ap);
    }
    assert(scan.count==STATION_MAX_NETWORKS);
    assert(strcmp(scan.networks[0].ssid,"Guest")==0);
    assert(strcmp(scan.networks[1].ssid,"Home")==0);
    puts("PASS: settings validation, rotation, calendar/DST and digit rendering");
}
