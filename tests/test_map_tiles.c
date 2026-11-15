#include <assert.h>
#include <setjmp.h>
#include "../firmware/main/map_tiles.c"
static int64_t clock_us=1000000;
static unsigned client_starts,client_reuses;
static jmp_buf done;static bool online;static unsigned char *fixture;static size_t fixture_size;static esp_http_client_config_t request;
int64_t esp_timer_get_time(void){return clock_us;}
void station_network_status(station_network_status_t *s){memset(s,0,sizeof(*s));s->connected=online;}
void esp_crt_bundle_attach(void){}
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle){(void)fn;(void)name;(void)arg;(void)handle;assert(stack==8192&&priority==1);return pdPASS;}
void vTaskDelay(unsigned ticks){(void)ticks;longjmp(done,1);}
void *esp_http_client_init(esp_http_client_config_t *c){client_starts++;request=*c;assert(c->crt_bundle_attach&&c->disable_auto_redirect);return (void*)1;}
int esp_http_client_perform(void *c){(void)c;esp_http_client_event_t e={.event_id=HTTP_EVENT_ON_DATA,.data=fixture,.data_len=(int)fixture_size,.user_data=request.user_data};return request.event_handler(&e);}
int esp_http_client_get_status_code(void *c){(void)c;return 200;}
void esp_http_client_cleanup(void *c){(void)c;}
int esp_http_client_set_url(void *c,const char *url){(void)c;assert(strstr(url,"https://tile.openstreetmap.org/"));client_reuses++;return ESP_OK;}
int esp_http_client_set_header(void *c,const char *name,const char *value){(void)c;(void)value;assert(strcmp(name,"apikey"));return ESP_OK;}
int main(void){
 lv_init();station_map_tile_t tile={0};assert(!station_map_tile_acquire(14,8295,5642,&tile));
 assert(tiles[0].state==QUEUED);tiles[0].pixels=malloc(131072);assert(tiles[0].pixels);tiles[0].state=READY;tiles[0].expires=clock_us+7000000;
 assert(station_map_tile_acquire(14,8295,5642,&tile)&&tile.handle==1&&tiles[0].refs==1);
 clock_us+=8000000;station_map_tile_t duplicate;assert(station_map_tile_acquire(14,8295,5642,&duplicate));
 for(int x=0;x<24;x++)station_map_tile_acquire(14,x,100,&duplicate);
 assert(tiles[0].refs==2&&tiles[0].x==8295);station_map_tile_release(1);station_map_tile_release(1);
 assert(!station_map_tile_acquire(14,8295,5642,&duplicate)&&tiles[0].state==QUEUED);
 station_map_cancel_pending();assert(tiles[0].state==EMPTY);
 response_t r={.data=malloc(PNG_LIMIT),.ttl=604800000000LL,.generation=viewport_generation};assert(r.data);
 esp_http_client_event_t e={.user_data=&r,.event_id=HTTP_EVENT_ON_HEADER,.header_key="Cache-Control",.header_value="max-age=3600"};assert(receive(&e)==ESP_OK&&r.ttl==3600000000LL);
 e.header_value="no-store";receive(&e);assert(!r.ttl);
 e.event_id=HTTP_EVENT_ON_DATA;e.data_len=PNG_LIMIT+1;assert(receive(&e)==ESP_FAIL&&r.overflow);
 station_map_cancel_pending();e.data_len=1;r.overflow=false;assert(receive(&e)==ESP_FAIL&&r.used==0);
 free(r.data);
 unsigned char *png=NULL,*rgba=NULL;size_t size=0;unsigned w=0,h=0;
 FILE *f=fopen("tests/fixtures/transport/map-tile.png","rb");assert(f);fseek(f,0,SEEK_END);size=(size_t)ftell(f);rewind(f);png=malloc(size);assert(png&&fread(png,1,size,f)==size);fclose(f);
 assert(!lodepng_decode32(&rgba,&w,&h,png,size)&&w==256&&h==256);

 lv_draw_buf_t *decoded=(lv_draw_buf_t *)rgba;unsigned char *raw=decoded->data;
 unsigned a=raw[3],red=(raw[0]*a+255*(255-a))/255,green=(raw[1]*a+255*(255-a))/255,blue=(raw[2]*a+255*(255-a))/255;
 uint16_t expected=(uint16_t)(((red>>3)<<11)|((green>>2)<<5)|(blue>>3));
 lv_draw_buf_destroy(decoded);fixture=png;fixture_size=size;online=true;
 assert(!station_map_tile_acquire(14,8295,5642,&tile));
 if(!setjmp(done))worker(NULL);
 assert(station_map_tile_acquire(14,8295,5642,&tile));assert(tile.pixels[0]==expected);station_map_tile_release(tile.handle);
 for(int x=8300;x<8340;x++){
  assert(!station_map_tile_acquire(14,x,5642,&tile));if(!setjmp(done))worker(NULL);
  assert(station_map_tile_acquire(14,x,5642,&tile)&&tile.pixels[0]==expected);station_map_tile_release(tile.handle);
 }
 unsigned before_revisit=client_starts;
 assert(station_map_tile_acquire(14,8295,5642,&tile));station_map_tile_release(tile.handle);
 assert(station_map_tile_acquire(14,8300,5642,&tile));station_map_tile_release(tile.handle);
 assert(client_starts==before_revisit); /* More than 24 tiles traversed, both old areas remain cached. */
 station_map_cancel_pending();unsigned starts=client_starts,reuses=client_reuses;
 assert(!station_map_tile_acquire(15,16000,11000,&tile));assert(!station_map_tile_acquire(15,16001,11000,&tile));
 if(!setjmp(done))worker(NULL);
 assert(client_starts==starts+1&&client_reuses==reuses+1);
 free(png);for(unsigned i=0;i<TILE_SLOTS;i++)free(tiles[i].pixels);lv_deinit();
 puts("PASS: map tile leases, eviction protection, expiry, cancellation, cache headers, response cap and real OSM PNG decode");
}
