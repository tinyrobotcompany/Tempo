#include "station_weather.h"
#include "cJSON.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void){
 FILE*f=fopen("tests/fixtures/weather.json","rb");assert(f);char json[24576];size_t n=fread(json,1,sizeof(json)-1,f);fclose(f);json[n]=0;
 station_weather_data_t d;
 assert(station_weather_parse(json,1794499201,&d));assert(d.valid&&fabsf(d.temperature-14.8f)<0.01f&&d.hour_count==12&&d.day_count==3);
 assert(d.hours[0].at==1794502800 && d.days[0].rain_chance==70 && d.direction==225);
 station_weather_plot_t plot;
 station_weather_plot(&d,1794499201,6,&plot);assert(plot.count==6&&plot.end-plot.start==5*3600);
 station_weather_plot(&d,1794499201,12,&plot);assert(plot.count==12&&station_weather_plot_join(&plot,1));
 station_weather_data_t flat=d;
 for(unsigned i=0;i<flat.hour_count;i++){flat.hours[i].temperature=-5;flat.hours[i].rain_chance=NAN;}
 flat.hours[0].rain_chance=0;
 station_weather_plot(&flat,1794499201,12,&plot);assert(plot.high-plot.low>=6&&plot.low<-5&&plot.high>-5);
 assert(plot.points[0].rain_chance==0&&isnan(plot.points[1].rain_chance));
 flat.hours[1].temperature=NAN;station_weather_plot(&flat,1794499201,12,&plot);
 assert(!station_weather_plot_join(&plot,1)&&!station_weather_plot_join(&plot,2)&&station_weather_plot_join(&plot,3));
 flat.hours[1]=flat.hours[2];station_weather_plot(&flat,1794499201,12,&plot);
 assert(!station_weather_plot_join(&plot,1));
 for(unsigned i=0;i<flat.hour_count;i++)flat.hours[i].temperature=NAN;
 station_weather_plot(&flat,1794499201,12,&plot);assert(isfinite(plot.low)&&plot.high>plot.low);
 station_weather_plot(&flat,1795000000,12,&plot);assert(plot.count==0&&plot.end>plot.start);
 station_weather_plot(NULL,1794499201,0,&plot);assert(plot.count==0&&plot.end>plot.start);
 assert(station_weather_day_index(&d,1794499201)==0);assert(station_weather_day_index(&d,1794585601)==1);assert(station_weather_day_index(&d,1795000000)==-1);
 assert(!station_weather_stale(&d,1794499300));assert(station_weather_stale(&d,1794501100));
 assert(!strcmp(station_weather_direction(225),"SW"));assert(!strcmp(station_weather_direction(NAN),"--"));assert(!strcmp(station_weather_condition(0,false),"Clear night"));
 assert(!station_weather_parse("{\"error\":true}",1794499201,&d));assert(!station_weather_parse("{}",1794499201,&d));assert(!station_weather_parse("{",1794499201,&d));
 cJSON*root=cJSON_Parse(json);cJSON*cur=cJSON_GetObjectItem(root,"current");cJSON_ReplaceItemInObject(cur,"temperature_2m",cJSON_CreateNull());
 cJSON_ReplaceItemInObject(cur,"relative_humidity_2m",cJSON_CreateNumber(200));
 cJSON_ReplaceItemInObject(cJSON_GetObjectItem(root,"current_units"),"wind_speed_10m",cJSON_CreateString("mph"));
 char*partial=cJSON_PrintUnformatted(root);assert(station_weather_parse(partial,1794499201,&d));assert(isnan(d.temperature)&&isnan(d.humidity)&&isnan(d.wind));free(partial);
 cJSON_DeleteItemFromObject(root,"hourly");cJSON_DeleteItemFromObject(root,"daily");partial=cJSON_PrintUnformatted(root);
 assert(station_weather_parse(partial,1794499201,&d)&&d.hour_count==0&&d.day_count==0);free(partial);
 cJSON_ReplaceItemInObject(cur,"time",cJSON_CreateNumber(1794499201+901));partial=cJSON_PrintUnformatted(root);assert(!station_weather_parse(partial,1794499201,&d));free(partial);cJSON_Delete(root);
 char url[1024];assert(station_weather_url(url,sizeof(url),48.8566,2.3522));assert(strlen(strchr(url+8,'/'))+sizeof("GET  HTTP/1.1\r\n")>512);assert(strstr(url,"timeformat=unixtime")&&strstr(url,"forecast_days=3"));assert(!station_weather_url(url,4,48,2));assert(!station_weather_url(url,sizeof(url),NAN,2));
 puts("PASS: weather schema, nulls, units, forecast selection, stale data, malformed responses and URL bounds");
}
