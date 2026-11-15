#pragma once
#include "lvgl.h"
#include "station_weather.h"
typedef struct {station_weather_plot_t plot;uint32_t accent;bool compact;} station_graph_visual_t;
typedef struct {int code;bool day;} station_icon_visual_t;
typedef struct {float low,high,minimum,maximum;uint32_t accent;} station_range_visual_t;
/* Caller owns state for the lifetime of the widget. Drawing does no I/O. */
lv_obj_t *station_graph_create(lv_obj_t *parent,int x,int y,int w,int h,station_graph_visual_t *state);
lv_obj_t *station_icon_create(lv_obj_t *parent,int x,int y,int size,station_icon_visual_t *state);
lv_obj_t *station_range_create(lv_obj_t *parent,int x,int y,int w,int h,station_range_visual_t *state);
