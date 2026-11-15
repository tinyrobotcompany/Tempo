#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#define TRANSPORT_STOPS 8
#define TRANSPORT_ROWS 6
#define TRANSPORT_ALERTS 8
typedef struct {char id[24],name[96],mode[24];unsigned metres,priority;
 double latitude,longitude;bool coordinates;
 char quays[16][24];unsigned quay_count;
} station_transport_stop_t;
typedef struct {char line[20],code[20],destination[128],status[24];time_t at[2];bool estimated;
 bool arrival[2],predicted[2],cancelled[2];int delay_minutes[2];
 char pickup[96],point[64],platform[24];
} station_transport_row_t;
typedef struct {
 unsigned count;bool loading,partial;
 station_transport_row_t rows[3];char stops[3][96],stop_ids[3][24];time_t fetched[3];
} station_transport_summary_t;
typedef enum {TRANS_LOCATION,TRANS_WIFI,TRANS_TIME,TRANS_KEY,TRANS_LOADING,TRANS_READY,TRANS_RETRY,TRANS_DENIED,TRANS_QUOTA,TRANS_EMPTY} station_transport_state_t;
typedef struct {
 station_transport_state_t state;
 station_transport_summary_t summary;
 unsigned generation,revision,count,selected,row_count,alert_count,selection_generation;
 station_transport_stop_t stops[TRANSPORT_STOPS];
 station_transport_row_t rows[TRANSPORT_ROWS];
 char alerts[TRANSPORT_ALERTS][192];
 bool valid,traffic_valid,traffic_partial,discovery_partial,discovering;
 time_t fetched_at,traffic_at;
} station_transport_snapshot_t;
bool station_transport_nearby_page(const char *json,station_transport_snapshot_t *out,bool reset,bool *more);
bool station_transport_nearby(const char *json,station_transport_snapshot_t *out);
bool station_transport_departures(const char *json,const char *stop_id,time_t now,station_transport_snapshot_t *out);
bool station_transport_line(const char *json,const char *line,char *code,size_t size);
bool station_transport_traffic(const char *json,const char *line,station_transport_snapshot_t *out);
time_t station_transport_timestamp(const char *text);
