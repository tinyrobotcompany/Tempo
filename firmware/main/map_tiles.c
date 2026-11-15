#include "map_tiles.h"
#include "network_time.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "src/libs/lodepng/lodepng.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#endif
/* RGB565 cache is bounded to 9 MiB and never consumes the TLS/internal heap. */
static void *map_alloc(size_t bytes){
#ifdef ESP_PLATFORM
 return heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
 return malloc(bytes);
#endif
}
#define TILE_SLOTS 72
#define PNG_LIMIT (128*1024)
typedef enum {EMPTY,QUEUED,LOADING,READY,FAILED} tile_state_t;
typedef struct {unsigned z,refs;int x,y;tile_state_t state;bool served;uint16_t *pixels;int64_t used,expires;uint64_t order;} tile_t;
static tile_t tiles[TILE_SLOTS];
static uint64_t queue_order;
static unsigned viewport_generation;
static portMUX_TYPE tile_mux=portMUX_INITIALIZER_UNLOCKED;
typedef struct {unsigned char *data;size_t used;bool overflow;int64_t ttl;unsigned generation;} response_t;
static esp_err_t receive(esp_http_client_event_t *e){
 response_t *r=e->user_data;
 portENTER_CRITICAL(&tile_mux);bool cancelled=r->generation!=viewport_generation;portEXIT_CRITICAL(&tile_mux);
 if(cancelled)return ESP_FAIL;
 if(e->event_id==HTTP_EVENT_ON_DATA&&e->data_len>0){
  if((size_t)e->data_len>PNG_LIMIT-r->used){r->overflow=true;return ESP_FAIL;}
  memcpy(r->data+r->used,e->data,e->data_len);r->used+=(size_t)e->data_len;
 }else if(e->event_id==HTTP_EVENT_ON_HEADER&&e->header_key&&e->header_value&&!strcasecmp(e->header_key,"Cache-Control")){
  const char *age=strstr(e->header_value,"max-age=");
  if(age){char *end;long long seconds=strtoll(age+8,&end,10);if(end!=age+8&&seconds>=0&&seconds<31536000)r->ttl=seconds*1000000LL;}
  if(strstr(e->header_value,"no-cache")||strstr(e->header_value,"no-store"))r->ttl=0;
 }
 return ESP_OK;
}
bool station_map_tile_acquire(unsigned z,int x,int y,station_map_tile_t *out){
 if(z<3||z>18||x<0||y<0||x>=(1<<z)||y>=(1<<z)||!out)return false;
 int64_t now=esp_timer_get_time();bool ready=false;int vacant=-1;
 portENTER_CRITICAL(&tile_mux);
 for(unsigned i=0;i<TILE_SLOTS;i++){
  tile_t *t=&tiles[i];
  if(t->state!=EMPTY&&t->z==z&&t->x==x&&t->y==y){
   t->used=now;
   if(t->state==READY&&(!t->served||t->refs||t->expires>now)){t->served=true;t->refs++;*out=(station_map_tile_t){.pixels=t->pixels,.handle=i+1};ready=true;}
   else if((t->state==FAILED||t->state==READY)&&t->expires<=now&&!t->refs){t->state=QUEUED;t->order=++queue_order;}
   portEXIT_CRITICAL(&tile_mux);return ready;
  }
  if(!t->refs&&t->state!=LOADING&&t->state!=QUEUED&&(vacant<0||t->used<tiles[vacant].used))vacant=(int)i;
 }
 if(vacant>=0){tile_t *t=&tiles[vacant];t->z=z;t->x=x;t->y=y;t->state=QUEUED;t->order=++queue_order;t->used=now;}
 portEXIT_CRITICAL(&tile_mux);return false;
}
void station_map_tile_release(unsigned handle){
 if(!handle||handle>TILE_SLOTS)return;
 portENTER_CRITICAL(&tile_mux);tile_t *t=&tiles[handle-1];if(t->refs)t->refs--;portEXIT_CRITICAL(&tile_mux);
}
void station_map_cancel_pending(void){portENTER_CRITICAL(&tile_mux);viewport_generation++;for(unsigned i=0;i<TILE_SLOTS;i++)if(tiles[i].state==QUEUED)tiles[i].state=EMPTY;portEXIT_CRITICAL(&tile_mux);}
static void worker(void *arg){
 (void)arg;
 esp_http_client_handle_t client=NULL;response_t r={0};
 for(;;){
  station_network_status_t net;station_network_status(&net);int slot=-1;unsigned z=0;int x=0,y=0;unsigned generation=0;
  if(net.connected){portENTER_CRITICAL(&tile_mux);for(unsigned i=0;i<TILE_SLOTS;i++)if(tiles[i].state==QUEUED&&(slot<0||tiles[i].order<tiles[slot].order))slot=(int)i;
   if(slot>=0){tiles[slot].state=LOADING;z=tiles[slot].z;x=tiles[slot].x;y=tiles[slot].y;generation=viewport_generation;}portEXIT_CRITICAL(&tile_mux);}
  if(slot<0){vTaskDelay(pdMS_TO_TICKS(100));continue;}
  int64_t started=esp_timer_get_time();
  char url[128];snprintf(url,sizeof(url),"https://tile.openstreetmap.org/%u/%d/%d.png",z,x,y);
  r=(response_t){.data=map_alloc(PNG_LIMIT),.ttl=604800000000LL,.generation=generation};bool ok=false;unsigned char *rgba=NULL;
  if(r.data){
   esp_http_client_config_t config={.url=url,.timeout_ms=8000,.buffer_size=2048,.crt_bundle_attach=esp_crt_bundle_attach,.disable_auto_redirect=true,.event_handler=receive,.user_data=&r};
   if(!client)client=esp_http_client_init(&config);else if(esp_http_client_set_url(client,url)!=ESP_OK){esp_http_client_cleanup(client);client=NULL;}
   esp_http_client_handle_t c=client;
   if(c){esp_http_client_set_header(c,"User-Agent","WeatherStation/0.8.0 (personal ESP32-P4 display)");
    esp_err_t err=esp_http_client_perform(c);
    portENTER_CRITICAL(&tile_mux);bool cancelled=generation!=viewport_generation;portEXIT_CRITICAL(&tile_mux);if(cancelled)err=ESP_FAIL;
    int code=esp_http_client_get_status_code(c);
    if(err!=ESP_OK){esp_http_client_cleanup(client);client=NULL;}
    /* Reject an unexpected image size before invoking the PNG allocator. */
    const unsigned char signature[]={137,80,78,71,13,10,26,10};
    if(err==ESP_OK&&code==200&&!r.overflow&&r.used>=33&&!memcmp(r.data,signature,8)&&!memcmp(r.data+12,"IHDR",4)&&r.data[16]==0&&r.data[17]==0&&r.data[18]==1&&r.data[19]==0&&r.data[20]==0&&r.data[21]==0&&r.data[22]==1&&r.data[23]==0){
     unsigned w=0,h=0;LodePNGState png;lodepng_state_init(&png);png.decoder.read_text_chunks=0;png.decoder.remember_unknown_chunks=0;png.info_raw.colortype=LCT_RGBA;png.info_raw.bitdepth=8;ok=lodepng_decode(&rgba,&w,&h,&png,r.data,r.used)==0&&w==256&&h==256;lodepng_state_cleanup(&png);
    }
   }
  }
  free(r.data);
  /* LVGL's bundled LodePNG returns lv_draw_buf_t, not a raw pixel array. */
  lv_draw_buf_t *decoded=(lv_draw_buf_t *)rgba;
  if(ok)ok=decoded&&decoded->data&&decoded->header.stride>=1024&&decoded->data_size>=decoded->header.stride*256;
  tile_t *t=&tiles[slot];if(ok&&!t->pixels)t->pixels=map_alloc(256*256*sizeof(uint16_t));if(!t->pixels)ok=false;
  if(ok)for(unsigned i=0;i<256*256;i++){const unsigned char *pixel=decoded->data+(i/256)*decoded->header.stride+(i%256)*4;unsigned a=pixel[3];unsigned red=(pixel[0]*a+255*(255-a))/255,green=(pixel[1]*a+255*(255-a))/255,blue=(pixel[2]*a+255*(255-a))/255;t->pixels[i]=(uint16_t)(((red>>3)<<11)|((green>>2)<<5)|(blue>>3));}
  if(decoded)lv_draw_buf_destroy(decoded);
#ifdef ESP_PLATFORM
  ESP_LOGI("street_map","Tile %u/%d/%d: %s, %u bytes, %lld ms",z,x,y,ok?"decoded":"failed/cancelled",(unsigned)r.used,(long long)((esp_timer_get_time()-started)/1000));
#else
  (void)started;
#endif
  portENTER_CRITICAL(&tile_mux);t->served=false;t->state=generation!=viewport_generation?EMPTY:ok?READY:FAILED;t->expires=esp_timer_get_time()+(ok?r.ttl:60000000);portEXIT_CRITICAL(&tile_mux);
 }
}
esp_err_t station_map_tiles_start(void){return xTaskCreate(worker,"map_tiles",8192,NULL,1,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;}
