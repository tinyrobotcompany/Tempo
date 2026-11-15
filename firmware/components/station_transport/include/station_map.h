#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {const uint16_t *pixels;unsigned handle;} station_map_tile_t;
/* Web Mercator, pixel coordinates at the requested zoom. */
void station_map_project(double lat,double lon,unsigned zoom,double *x,double *y);
void station_map_unproject(double x,double y,unsigned zoom,double *lat,double *lon);
