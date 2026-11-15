#pragma once
#include <stdint.h>
#include "station_transport.h"
#define TRANSPORT_LINE_STOPS 80
#define TRANSPORT_SEARCH_STOPS 80
typedef enum {LOOKUP_IDLE,LOOKUP_LOADING,LOOKUP_READY,LOOKUP_FAILED} station_lookup_state_t;
typedef struct {station_lookup_state_t state;unsigned revision,count;station_transport_stop_t stops[TRANSPORT_SEARCH_STOPS];char services[TRANSPORT_SEARCH_STOPS][128];bool connections,partial;char query[96];} station_transport_search_t;
typedef struct {char id[24],area[24],name[96],mode[24],connections[384];bool connections_known;double latitude,longitude;bool coordinates;} station_line_stop_t;
typedef struct {char name[196];unsigned count;uint8_t stops[TRANSPORT_LINE_STOPS];} station_line_route_t;
typedef struct {station_lookup_state_t state;unsigned revision,count;unsigned route_count;uint32_t color;station_line_route_t routes[16];bool partial,connections_loading,connections_failed;char line[20];station_line_stop_t stops[TRANSPORT_LINE_STOPS];} station_transport_line_map_t;
bool station_transport_search_parse(const char *json,double home_lat,double home_lon,station_transport_search_t *out);
bool station_transport_line_stops_parse(const char *json,station_transport_line_map_t *out);

bool station_transport_routes_parse(const char *json,station_transport_line_map_t *out);

bool station_transport_interchanges_parse(const char *json,station_transport_line_map_t *out);
