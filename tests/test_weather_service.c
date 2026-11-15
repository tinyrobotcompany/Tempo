/* Runs the real worker loop with deterministic HTTP/network/time seams. */
#include "platform.h"
#include "weather_service.h"
#include "network_time.h"
#include "cJSON.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
static void (*worker)(void*);
static jmp_buf finished;
static station_network_status_t home;
static esp_http_client_config_t config;
static int64_t us;
static unsigned step,fetches;
static enum {GOOD,HTTP_ERROR,BAD_JSON,OVERSIZE} response;
static bool move_during_fetch;
static char fixture[24576];
time_t test_time(time_t *t){time_t now=1794499201+us/1000000;if(t)*t=now;return now;}
int64_t esp_timer_get_time(void){return us;}
void station_network_status(station_network_status_t*s){*s=home;}
int xTaskCreate(void(*fn)(void*),const char*n,unsigned stack,void*a,unsigned p,void*h){(void)n;(void)stack;(void)a;(void)p;(void)h;worker=fn;return pdPASS;}
static char last_log[512];
void test_log(const char*t,const char*f,...){(void)t;va_list args;va_start(args,f);vsnprintf(last_log,sizeof(last_log),f,args);va_end(args);
 assert(!strstr(last_log,"latitude")&&!strstr(last_log,"longitude")&&!strstr(last_log,"https://"));}
const char*esp_err_to_name(int e){return e==ESP_OK?"OK":"FAIL";}
void esp_crt_bundle_attach(void){}
void*esp_http_client_init(esp_http_client_config_t*c){config=*c;assert(strstr(c->url,"https://api.open-meteo.com/")&&c->crt_bundle_attach&&c->disable_auto_redirect);return &config;}
int esp_http_client_perform(void*client){
 (void)client;fetches++;
 /* ESP-IDF defaults to 512 TX bytes and must fit the entire request line. */
 const char *path=strchr(config.url+strlen("https://"),'/');assert(path);
 int capacity=config.buffer_size_tx?config.buffer_size_tx:512;
 char request[4096];assert(capacity>0&&capacity<=(int)sizeof(request));
 int length=snprintf(request,(size_t)capacity,"GET %s HTTP/1.1\r\n",path);
 if(length>=capacity)return ESP_FAIL;
 if(move_during_fetch){home.location_generation++;home.latitude=51.5;strcpy(home.location_name,"London / GB");move_during_fetch=false;}
 esp_http_client_event_t event={.event_id=HTTP_EVENT_ON_DATA,.user_data=config.user_data};
 if(response==OVERSIZE){event.data="x";event.data_len=24576;config.event_handler(&event);return ESP_OK;}
 cJSON*root=cJSON_Parse(fixture);assert(root);cJSON_ReplaceItemInObject(cJSON_GetObjectItem(root,"current"),"time",cJSON_CreateNumber(test_time(NULL)));
 char*body=cJSON_PrintUnformatted(root);cJSON_Delete(root);
 if(response==BAD_JSON){free(body);body=malloc(2);strcpy(body,"{");}
 size_t n=strlen(body);event.data=body;event.data_len=n/2;config.event_handler(&event);event.data=body+n/2;event.data_len=n-n/2;config.event_handler(&event);free(body);return ESP_OK;
}
int esp_http_client_get_status_code(void*c){(void)c;return response==HTTP_ERROR?503:200;}
void esp_http_client_cleanup(void*c){(void)c;}
void vTaskDelay(unsigned ticks){
 (void)ticks;station_weather_snapshot_t s;station_weather_status(&s);
 switch(++step){
 case 1:assert(s.state==WEATHER_LOCATION&&fetches==0);home.location_valid=true;home.location_generation=1;home.latitude=48.85;home.longitude=2.35;strcpy(home.location_name,"Paris / FR");break;
 case 2:assert(s.state==WEATHER_WIFI);home.connected=true;break;
 case 3:assert(s.state==WEATHER_TIME&&fetches==0);home.source=TIME_NETWORK;break;
 case 4:assert(s.state==WEATHER_READY&&s.data.valid&&fetches==1&&!s.stale);assert(!station_weather_refresh());home.connected=false;us+=1000000;break;
 case 5:assert(s.state==WEATHER_WIFI&&s.data.valid&&fetches==1);home.connected=true;us+=59000000;break;
 case 6:assert(s.state==WEATHER_READY&&fetches==2);response=HTTP_ERROR;us+=60000000;assert(station_weather_refresh());break;
 case 7:assert(strstr(last_log,"reason=http")&&strstr(last_log,"elapsed="));assert(s.state==WEATHER_RETRY&&s.data.valid&&fetches==3);response=BAD_JSON;us+=60000000;break;
 case 8:assert(strstr(last_log,"reason=parse"));assert(s.state==WEATHER_RETRY&&s.data.valid&&fetches==4);response=OVERSIZE;us+=120000000;break;
 case 9:assert(strstr(last_log,"reason=overflow"));assert(s.state==WEATHER_RETRY&&s.data.valid&&fetches==5);response=GOOD;us+=240000000;move_during_fetch=true;break;
 case 10:assert(fetches==6&&s.location_generation==1&&home.location_generation==2);break;
 case 11:assert(fetches==7&&s.location_generation==2&&s.state==WEATHER_READY&&!strcmp(s.location,"London / GB"));home.connected=false;us+=1801000000LL;break;
 case 12:assert(s.state==WEATHER_WIFI&&s.stale&&s.data.valid&&fetches==7);home.connected=true;break;
 case 13:assert(s.state==WEATHER_READY&&!s.stale&&fetches==8);assert(!station_weather_refresh());longjmp(finished,1);
 default:abort();
 }
}
int main(void){FILE*f=fopen("tests/fixtures/weather.json","rb");assert(f);size_t n=fread(fixture,1,sizeof(fixture)-1,f);fclose(f);fixture[n]=0;
 assert(station_weather_start()==ESP_OK&&worker);if(setjmp(finished)==0)worker(NULL);
 puts("PASS: real weather worker: prerequisites, HTTPS chunks, cache, rate limit, retry/backoff, overflow, stale data, reconnect, in-flight location change");}
