/* Executes the real network/clock workers with deterministic driver and RTOS seams. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network_time.h"
#include "platform.h"

static jmp_buf stop_worker;
static void (*clock_task)(void *), (*network_task)(void *);
static test_event_handler_t scan_handler, ip_handler;
static unsigned wifi_calls, time_calls, receive_count;
static time_t actual_time;
static bool fail_clock, fail_scan, omit_scan_event;
static char stored_place[64],stored_coords[64],stored_zone[64];
static uint8_t zone_pending;
static bool capture_wifi,restore_wifi;
static unsigned char saved_profiles[1024],saved_wifi[97];
static size_t saved_profiles_size;
static unsigned profile_writes;
static wifi_config_t last_config;
static unsigned result_count=3;
static int64_t monotonic_us;
struct test_queue {size_t size; unsigned count; unsigned char data[4][384];};
QueueHandle_t xQueueCreate(unsigned count,size_t size) {assert(count<=4 && size<=384); struct test_queue *q=calloc(1,sizeof(*q)); q->size=size; return q;}
int xQueueSend(QueueHandle_t q,const void *item,unsigned ticks) {(void)ticks;if(q->count==4)return 0;memcpy(q->data[q->count++],item,q->size);return pdTRUE;}
int xQueueReceive(QueueHandle_t q,void *item,unsigned ticks) {
    (void)ticks;
    if (receive_count++ == 1) longjmp(stop_worker,1);
    if(!q->count)return 0;
    memcpy(item,q->data[0],q->size);q->count--;memmove(q->data[0],q->data[1],q->count*384);return pdTRUE;
}
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle) {
    (void)stack;(void)arg;(void)priority;(void)handle;
    if(strcmp(name,"station_clock")==0)clock_task=fn; else network_task=fn; return pdPASS;
}
static void run_one(void (*task)(void *)) {receive_count=0;if(setjmp(stop_worker)==0) task(NULL);}
void test_log(const char *tag,const char *format,...) {(void)tag;(void)format;}
const char *esp_err_to_name(int err) {(void)err;return "test";}
int64_t esp_timer_get_time(void) {return monotonic_us;}
int test_settimeofday(const struct timeval *tv,const struct timezone *tz) {(void)tz;time_calls++;if(fail_clock)return -1;actual_time=tv->tv_sec;return 0;}
int nvs_open(const char *n,int mode,nvs_handle_t *h){(void)n;(void)mode;*h=1;return ESP_OK;}
void nvs_close(nvs_handle_t h){(void)h;}
int nvs_get_u8(nvs_handle_t h,const char*k,uint8_t*v){(void)h;if(!strcmp(k,"zone_pending")){*v=zone_pending;return ESP_OK;}return ESP_ERR_NVS_NOT_FOUND;}
int nvs_get_str(nvs_handle_t h,const char*k,char*v,size_t*s){(void)h;const char *source=!strcmp(k,"place")?stored_place:!strcmp(k,"location")?stored_coords:!strcmp(k,"zone")?stored_zone:NULL;if(!source||!*source)return ESP_ERR_NVS_NOT_FOUND;assert(*s>strlen(source));strcpy(v,source);*s=strlen(source)+1;return ESP_OK;}
int nvs_get_blob(nvs_handle_t h,const char*k,void*v,size_t*s){(void)h;
 if(capture_wifi&&!strcmp(k,"wifi_profiles")&&saved_profiles_size){assert(*s>=saved_profiles_size);memcpy(v,saved_profiles,saved_profiles_size);*s=saved_profiles_size;return ESP_OK;}
 if(restore_wifi&&!strcmp(k,"wifi")){assert(*s>=sizeof(saved_wifi));memcpy(v,saved_wifi,sizeof(saved_wifi));*s=sizeof(saved_wifi);return ESP_OK;}
 return ESP_ERR_NVS_NOT_FOUND;}
int nvs_set_u8(nvs_handle_t h,const char*k,uint8_t v){(void)h;if(!strcmp(k,"zone_pending"))zone_pending=v;return ESP_OK;}
int nvs_set_str(nvs_handle_t h,const char*k,const char*v){(void)h;char *dest=!strcmp(k,"place")?stored_place:!strcmp(k,"location")?stored_coords:!strcmp(k,"zone")?stored_zone:NULL;if(dest){assert(strlen(v)<64);strcpy(dest,v);}return ESP_OK;}
int nvs_set_blob(nvs_handle_t h,const char*k,const void*v,size_t s){(void)h;
 if(capture_wifi&&!strcmp(k,"wifi_profiles")){assert(s<=sizeof(saved_profiles));memcpy(saved_profiles,v,s);saved_profiles_size=s;profile_writes++;}
 if(capture_wifi&&!strcmp(k,"wifi")){assert(s==sizeof(saved_wifi));memcpy(saved_wifi,v,s);}
 return ESP_OK;}
int nvs_erase_key(nvs_handle_t h,const char*k){(void)h;(void)k;return ESP_OK;}
int nvs_commit(nvs_handle_t h){(void)h;return ESP_OK;}
const char *WIFI_EVENT="wifi", *IP_EVENT="ip";
int esp_event_handler_register(esp_event_base_t base,int32_t id,test_event_handler_t h,void *arg){(void)base;(void)arg;if(id==WIFI_EVENT_SCAN_DONE)scan_handler=h;if(base==IP_EVENT)ip_handler=h;return ESP_OK;}
int esp_event_loop_create_default(void){return ESP_OK;}
int esp_netif_init(void){return ESP_OK;}
void *esp_netif_create_default_wifi_sta(void){return (void*)1;}
int esp_wifi_init(wifi_init_config_t*c){(void)c;wifi_calls++;return ESP_OK;}
int esp_wifi_set_storage(int x){(void)x;return ESP_OK;}
int esp_wifi_set_mode(int x){(void)x;return ESP_OK;}
int esp_wifi_disconnect(void){return ESP_OK;}
int esp_wifi_stop(void){return ESP_OK;}
int esp_wifi_start(void){return ESP_OK;}
int esp_wifi_connect(void){wifi_calls++;return ESP_OK;}
int esp_wifi_set_config(int x,wifi_config_t*c){(void)x;last_config=*c;return ESP_OK;}
int esp_wifi_scan_start(wifi_scan_config_t*c,bool blocking){
    (void)c;assert(!blocking);wifi_calls++;
    if(fail_scan)return ESP_FAIL;
    if (omit_scan_event) return ESP_OK;
    wifi_event_sta_scan_done_t done={.status=0};scan_handler(NULL,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,&done);return ESP_OK;
}
int esp_wifi_scan_stop(void){return ESP_OK;}
int esp_wifi_clear_ap_list(void){return ESP_OK;}
int esp_wifi_scan_get_ap_records(uint16_t*n,wifi_ap_record_t*r){
    assert(*n>=3);*n=result_count;
    r[0]=(wifi_ap_record_t){.ssid="Home",.rssi=-30,.authmode=1};
    r[1]=(wifi_ap_record_t){.ssid="Home",.rssi=-70,.authmode=1};
    r[2]=(wifi_ap_record_t){.ssid="Guest",.rssi=-60,.authmode=WIFI_AUTH_OPEN};return ESP_OK;
}
int esp_netif_sntp_init(esp_sntp_config_t*c){(void)c;return ESP_OK;}
void esp_netif_sntp_deinit(void){}
int esp_netif_sntp_start(void){return ESP_OK;}
void esp_crt_bundle_attach(void){}
void *esp_http_client_init(esp_http_client_config_t*c){(void)c;return NULL;}
int esp_http_client_perform(void*c){(void)c;return ESP_FAIL;}
int esp_http_client_get_status_code(void*c){(void)c;return 500;}
void esp_http_client_cleanup(void*c){(void)c;}

int main(void) {
    assert(station_network_start()==ESP_OK && clock_task && network_task);
    /* Leave a Wi-Fi request queued: the clock must complete without running Wi-Fi. */
    assert(station_network_scan());
    assert(station_network_manual(1794486840));
    run_one(clock_task);
    station_network_status_t status;station_network_status(&status);
    assert(actual_time==1794486840 && time_calls==1 && wifi_calls==0);
    assert(status.source==TIME_MANUAL && !status.automatic && status.time_success && status.time_completion==1);
    run_one(network_task);
    station_wifi_scan_t scan;station_network_scan_status(&scan);
    assert(scan.state==WIFI_SCAN_READY && scan.count==2);
    assert(strcmp(scan.networks[0].ssid,"Home")==0 && scan.networks[0].rssi==-30);
    assert(!scan.networks[1].secured);
    fail_scan=true;assert(station_network_scan());run_one(network_task);station_network_scan_status(&scan);
    assert(scan.state==WIFI_SCAN_FAILED);
    fail_scan=false;omit_scan_event=true;assert(station_network_scan());run_one(network_task);
    station_network_scan_status(&scan); assert(scan.state==WIFI_SCAN_RUNNING);
    monotonic_us=21000000;run_one(network_task);station_network_scan_status(&scan);
    assert(scan.state==WIFI_SCAN_FAILED && strstr(scan.message,"timed out"));
    omit_scan_event=false;result_count=0;assert(station_network_scan());run_one(network_task);
    station_network_scan_status(&scan);assert(scan.state==WIFI_SCAN_READY && scan.count==0);
    fail_clock=true;assert(station_network_manual(1794486900));run_one(clock_task);station_network_status(&status);
    assert(!status.time_success && actual_time==1794486840);
    assert(station_network_connect("Home","12345678"));station_network_status(&status);assert(status.connecting && !status.connected);
    assert(!station_network_connect("Home","12345678"));run_one(network_task);
    ip_handler(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);station_network_status(&status);assert(status.connected && !status.connecting);
    assert(station_network_connect("Home","badpassword"));run_one(network_task);
    monotonic_us+=46000000;run_one(network_task);station_network_status(&status);assert(!status.connecting && strstr(status.message,"timed out"));
    unsigned paris=0;for(unsigned i=0;i<station_city_count;i++)if(!strcmp(station_cities[i].zone,"Europe/Paris"))paris=i;
    assert(station_network_city(paris));run_one(network_task);station_network_status(&status);
    assert(status.location_success && status.location_completion==1 && !strcmp(status.timezone,"Europe/Paris"));
    assert(status.location_valid && status.location_generation==1 && status.latitude>48 && status.latitude<49 && !strcmp(status.location_name,"Paris / FR"));
    assert(!station_network_home("NaN,2","Bad"));assert(!station_network_home("48,2","Bad\nname"));
    assert(station_network_home("48.810000,2.220000","Home suburb"));run_one(network_task);station_network_status(&status);
    assert(status.location_completion==2&&status.location_generation>1&&!strcmp(status.location_name,"Home suburb"));
    assert(status.latitude==48.81&&status.longitude==2.22&&!strcmp(status.timezone,"Europe/Paris"));
    assert(!strcmp(stored_place,"Home suburb")&&!strcmp(stored_coords,"48.810000,2.220000")&&zone_pending==1);
    run_one(network_task);station_network_status(&status);assert(!strcmp(status.location_name,"Home suburb")&&!strcmp(status.timezone,"Europe/Paris"));
    capture_wifi=true;
    ip_handler(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);run_one(network_task);
    assert(station_network_connect("Home","correct-password"));run_one(network_task);
    unsigned before=profile_writes;
    assert(!strcmp((char*)last_config.sta.password,"correct-password"));
    assert(profile_writes==before);
    ip_handler(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);run_one(network_task);
    assert(profile_writes==before+1&&!strcmp((char*)saved_wifi+33,"correct-password"));
    assert(station_network_connect("Other","other-password"));run_one(network_task);
    ip_handler(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);run_one(network_task);
    assert(station_network_connect("Home",""));run_one(network_task);
    assert(!strcmp((char*)last_config.sta.password,"correct-password"));
    ip_handler(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);run_one(network_task);
    before=profile_writes;
    assert(station_network_connect("Home","wrong-password"));run_one(network_task);
    monotonic_us+=46000000;run_one(network_task);
    assert(profile_writes==before&&!strcmp((char*)saved_wifi+33,"correct-password"));
    assert(station_network_connect("Home",""));run_one(network_task);
    assert(!strcmp((char*)last_config.sta.password,"correct-password"));
    ip_handler(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);run_one(network_task);
    result_count=3;assert(station_network_scan());run_one(network_task);station_network_scan_status(&scan);
    assert(scan.networks[0].saved&&!scan.networks[1].saved);
    restore_wifi=true;run_one(network_task);assert(!strcmp((char*)last_config.sta.ssid,"Home")&&!strcmp((char*)last_config.sta.password,"correct-password"));
    puts("PASS: successful networks remembered, blank reuses credentials, failed replacement preserves password, saved scan and restart");
    puts("PASS: precise home coordinates/name persist and previous zone survives pending resolution");
    puts("PASS: real clock worker progresses while Wi-Fi queued; time failure reported; async scan and deduplication");
}
