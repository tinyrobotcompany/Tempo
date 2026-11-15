#include "station_weather.h"
#include "cJSON.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static const cJSON *item(const cJSON *o,const char *key) { return cJSON_GetObjectItemCaseSensitive(o,key); }
static float number(const cJSON *v,double low,double high)
{
    return cJSON_IsNumber(v)&&isfinite(v->valuedouble)&&v->valuedouble>=low&&v->valuedouble<=high ? (float)v->valuedouble : NAN;
}
static time_t timestamp(const cJSON *v)
{
    if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<1704067200.0||v->valuedouble>4102444800.0||floor(v->valuedouble)!=v->valuedouble) return 0;
    return (time_t)v->valuedouble;
}
static int code(const cJSON *v)
{
    float n=number(v,0,99); return isfinite(n)&&floorf(n)==n?(int)n:-1;
}
static float field(const cJSON *object,const cJSON *units,const char *key,const char *unit,int index,double low,double high)
{
    const cJSON *u=item(units,key),*v=item(object,key);
    if(!cJSON_IsString(u)||strcmp(u->valuestring,unit)) return NAN;
    if(index>=0) v=cJSON_GetArrayItem(v,index);
    return number(v,low,high);
}
bool station_weather_parse(const char *json,time_t fetched_at,station_weather_data_t *out)
{
    if(!json||!out) return false;
    const char *end=NULL;
    cJSON *root=cJSON_ParseWithOpts(json,&end,true);
    if(!cJSON_IsObject(root)||cJSON_IsTrue(item(root,"error"))) { cJSON_Delete(root); return false; }
    const cJSON *cur=item(root,"current"),*units=item(root,"current_units");
    station_weather_data_t d={.fetched_at=fetched_at};
    d.observed_at=timestamp(item(cur,"time"));
    if(!d.observed_at || d.observed_at>fetched_at+900) {cJSON_Delete(root);return false;}
    float interval=number(item(cur,"interval"),1,3600);
    d.interval_seconds=isfinite(interval)?(int)interval:0;
    d.temperature=field(cur,units,"temperature_2m","°C",-1,-100,70);
    d.feels_like=field(cur,units,"apparent_temperature","°C",-1,-120,90);
    d.humidity=field(cur,units,"relative_humidity_2m","%",-1,0,100);
    d.wind=field(cur,units,"wind_speed_10m","km/h",-1,0,500);
    d.gusts=field(cur,units,"wind_gusts_10m","km/h",-1,0,500);
    d.direction=field(cur,units,"wind_direction_10m","°",-1,0,360);
    d.pressure=field(cur,units,"pressure_msl","hPa",-1,600,1200);
    d.precipitation=field(cur,units,"precipitation","mm",-1,0,1000);
    d.code=code(item(cur,"weather_code")); d.is_day=number(item(cur,"is_day"),0,1)==1;
    d.valid=isfinite(d.temperature)||isfinite(d.humidity)||d.code>=0;
    float offset=number(item(root,"utc_offset_seconds"),-50400,50400); d.utc_offset=isfinite(offset)?(int)offset:0;
    const cJSON *hour=item(root,"hourly"),*hu=item(root,"hourly_units"),*times=item(hour,"time");
    int n=cJSON_GetArraySize(times); if(n>96)n=96;
    time_t previous=0;
    for(int i=0;i<n && d.hour_count<WEATHER_HOURS;i++) {
        time_t t=timestamp(cJSON_GetArrayItem(times,i));
        if(!t||t<fetched_at||t<=previous) continue;
        previous=t;
        station_weather_hour_t *h=&d.hours[d.hour_count++]; h->at=t;
        h->temperature=field(hour,hu,"temperature_2m","°C",i,-100,70);
        h->rain_chance=field(hour,hu,"precipitation_probability","%",i,0,100);
        h->code=code(cJSON_GetArrayItem(item(hour,"weather_code"),i));
    }
    const cJSON *day=item(root,"daily"),*du=item(root,"daily_units"); times=item(day,"time"); n=cJSON_GetArraySize(times); if(n>16)n=16;
    previous=0;
    for(int i=0;i<n && d.day_count<WEATHER_DAYS;i++) {
        time_t t=timestamp(cJSON_GetArrayItem(times,i)); if(!t||t<=previous) continue; previous=t;
        station_weather_day_t *a=&d.days[d.day_count++]; a->at=t;
        a->high=field(day,du,"temperature_2m_max","°C",i,-100,70);
        a->low=field(day,du,"temperature_2m_min","°C",i,-100,70);
        a->rain_chance=field(day,du,"precipitation_probability_max","%",i,0,100);
        a->precipitation=field(day,du,"precipitation_sum","mm",i,0,3000);
        a->code=code(cJSON_GetArrayItem(item(day,"weather_code"),i));
    }
    cJSON_Delete(root); if(!d.valid)return false; *out=d; return true;
}
bool station_weather_url(char *url,size_t size,double lat,double lon)
{
    if(!url||!size||!isfinite(lat)||!isfinite(lon)||lat< -90||lat>90||lon< -180||lon>180)return false;
    int n=snprintf(url,size,"https://api.open-meteo.com/v1/forecast?latitude=%.6f&longitude=%.6f"
        "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,precipitation,weather_code,pressure_msl,wind_speed_10m,wind_direction_10m,wind_gusts_10m"
        "&hourly=temperature_2m,precipitation_probability,weather_code"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,precipitation_sum"
        "&timezone=auto&timeformat=unixtime&forecast_days=3&forecast_hours=13&temperature_unit=celsius&wind_speed_unit=kmh&precipitation_unit=mm",lat,lon);
    return n>=0&&(size_t)n<size;
}
const char *station_weather_condition(int c,bool day)
{
    switch(c) {
    case 0:return day?"Clear sky":"Clear night"; case 1:return "Mostly clear";case 2:return "Partly cloudy";case 3:return "Overcast";
    case 45:case 48:return "Fog";
    case 51:case 53:case 55:return "Drizzle";case 56:case 57:return "Freezing drizzle";
    case 61:return "Light rain";case 63:return "Rain";case 65:return "Heavy rain";case 66:case 67:return "Freezing rain";
    case 71:case 73:case 75:case 77:return "Snow";case 80:case 81:case 82:return "Rain showers";
    case 85:case 86:return "Snow showers";case 95:case 96:case 99:return "Thunderstorms";
    default:return "Conditions unavailable";
    }
}
const char *station_weather_direction(float deg)
{
    static const char *dirs[]={"N","NE","E","SE","S","SW","W","NW"};
    return isfinite(deg)&&deg>=0&&deg<=360?dirs[(unsigned)((deg+22.5f)/45.0f)%8]:"--";
}
bool station_weather_stale(const station_weather_data_t *d,time_t now)
{
    return !d||!d->valid||now<d->fetched_at||now-d->fetched_at>=1800||now-d->observed_at>=1800;
}

int station_weather_day_index(const station_weather_data_t *d,time_t now)
{
    if(!d || !d->valid)return -1;
    for(unsigned i=0;i<d->day_count;i++)
        if((d->days[i].at+d->utc_offset)/86400==(now+d->utc_offset)/86400)return (int)i;
    return -1;
}

void station_weather_plot(const station_weather_data_t *d,time_t now,unsigned hours,station_weather_plot_t *p)
{
    memset(p,0,sizeof(*p));
    if(hours<2)hours=2;
    if(hours>WEATHER_HOURS)hours=WEATHER_HOURS;
    p->start=(now/3600+1)*3600;p->end=p->start+(hours-1)*3600;
    p->low=0;p->high=6;
    if(!d||!d->valid)return;
    float low=INFINITY,high=-INFINITY;
    for(unsigned i=0;i<d->hour_count && i<WEATHER_HOURS;i++) {
        const station_weather_hour_t *h=&d->hours[i];
        if(h->at<now)continue;
        if(!p->count){p->start=h->at;p->end=h->at+(hours-1)*3600;}
        if(h->at>p->end)break;
        if(p->count && h->at<=p->points[p->count-1].at)continue;
        p->points[p->count++]=*h;
        if(isfinite(h->temperature)){if(h->temperature<low)low=h->temperature;if(h->temperature>high)high=h->temperature;}
    }
    if(isfinite(low)) {
        float pad=fmaxf(1.0f,(6.0f-(high-low))/2.0f);
        p->low=floorf(low-pad);p->high=ceilf(high+pad);
    }
}
bool station_weather_plot_join(const station_weather_plot_t *p,unsigned right)
{
    return p && right>0 && right<p->count && isfinite(p->points[right-1].temperature) &&
        isfinite(p->points[right].temperature) && p->points[right].at-p->points[right-1].at<=3600 &&
        p->points[right].at>p->points[right-1].at;
}
