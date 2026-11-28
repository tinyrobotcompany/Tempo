/* Real settings persistence and asynchronous worker, with an in-memory NVS seam. */
#include "platform.h"
#define ESP_ERR_INVALID_VERSION 4
#define portMAX_DELAY UINT32_MAX
int xQueueOverwrite(QueueHandle_t, const void *);
#include "../firmware/main/settings.c"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
static uint8_t blob[STATION_SETTINGS_BYTES];
static size_t blob_size;
static uint8_t v2[STATION_SETTINGS_V2_BYTES];
static int open_error, read_error, write_error, commit_error;
static bool legacy_only, queue_fail, task_fail;
static unsigned closes, commits, writes;
static save_request_t queued;
static bool queued_valid;
static jmp_buf done;
struct test_queue { int unused; } queue;
int nvs_open(const char*n,int mode,nvs_handle_t*h){assert(!strcmp(n,"station"));(void)mode;*h=1;return open_error;}
void nvs_close(nvs_handle_t h){assert(h==1);closes++;}
int nvs_get_blob(nvs_handle_t h,const char*k,void*out,size_t*n){(void)h;if(legacy_only&&(!strcmp(k,"settings_v4")||!strcmp(k,"settings_v3")||!strcmp(k,"settings_v2")))return ESP_ERR_NVS_NOT_FOUND;if(read_error)return read_error;assert(*n>=blob_size);memcpy(out,blob,blob_size);*n=blob_size;return ESP_OK;}
int nvs_set_blob(nvs_handle_t h,const char*k,const void*in,size_t n){(void)h;writes++;if(write_error)return write_error;if(!strcmp(k,"settings_v4")){memcpy(blob,in,n);blob_size=n;}else if(!strcmp(k,"settings_v3")){assert(n==STATION_SETTINGS_V3_BYTES);assert(((const uint8_t*)in)[59]==0);}else if(!strcmp(k,"settings_v2")){assert(n==sizeof(v2));memcpy(v2,in,n);}else assert(!strcmp(k,"settings")&&n==6);return ESP_OK;}
int nvs_commit(nvs_handle_t h){(void)h;commits++;return commit_error;}
/* Local mode flag: a separate u8 key, never part of the settings blob. */
static uint8_t local_value,local_pending;static bool local_present;static int local_read_error,local_write_error;
int nvs_get_u8(nvs_handle_t h,const char*k,uint8_t*out){(void)h;assert(!strcmp(k,"local_mode"));if(local_read_error)return local_read_error;if(!local_present)return ESP_ERR_NVS_NOT_FOUND;*out=local_value;return ESP_OK;}
int nvs_set_u8(nvs_handle_t h,const char*k,uint8_t v){(void)h;assert(!strcmp(k,"local_mode"));if(local_write_error)return local_write_error;local_pending=v;return ESP_OK;}
static void local_mode_tests(void){
 station_local_mode_t mode=STATION_LOCAL_MODE_ON;
 /* New device: no namespace or no key means Local mode is off (port 80 closed). */
 open_error=ESP_ERR_NVS_NOT_FOUND;assert(station_local_mode_load(&mode)==ESP_OK&&mode==STATION_LOCAL_MODE_OFF);open_error=0;
 mode=STATION_LOCAL_MODE_ON;assert(station_local_mode_load(&mode)==ESP_OK&&mode==STATION_LOCAL_MODE_OFF);
 open_error=ESP_FAIL;assert(station_local_mode_load(&mode)==ESP_FAIL&&mode==STATION_LOCAL_MODE_OFF);assert(station_local_mode_save(STATION_LOCAL_MODE_ON)==ESP_FAIL);open_error=0;
 local_read_error=ESP_FAIL;assert(station_local_mode_load(&mode)==ESP_FAIL&&mode==STATION_LOCAL_MODE_OFF);local_read_error=0;
 assert(station_local_mode_save((station_local_mode_t)3)==ESP_ERR_INVALID_ARG);
 local_write_error=ESP_FAIL;assert(station_local_mode_save(STATION_LOCAL_MODE_ON)==ESP_FAIL);local_write_error=0;
 commit_error=ESP_FAIL;assert(station_local_mode_save(STATION_LOCAL_MODE_ON)==ESP_FAIL);commit_error=0;
 /* Survives restart: a committed value is read back. */
 for(unsigned v=0;v<3;v++){assert(station_local_mode_save((station_local_mode_t)v)==ESP_OK&&local_pending==v);local_value=local_pending;local_present=true;assert(station_local_mode_load(&mode)==ESP_OK&&mode==(station_local_mode_t)v);}
 /* Corrupt values never turn Local mode on; they still force the token replacement. */
 local_value=9;assert(station_local_mode_load(&mode)==ESP_OK&&mode==STATION_LOCAL_MODE_ROTATE);
 /* Saving appearance settings never touches the Local mode key. */
 local_value=0;station_settings_t s=station_defaults();assert(station_settings_save(&s)==ESP_OK&&local_value==0);
 local_present=false;blob_size=0;memset(blob,0,sizeof(blob));
}
QueueHandle_t xQueueCreate(unsigned n,size_t size){assert(n==1&&size==sizeof(save_request_t));return queue_fail?NULL:&queue;}
int xQueueOverwrite(QueueHandle_t q,const void*item){assert(q==&queue);queued=*(const save_request_t*)item;queued_valid=true;return pdTRUE;}
int xQueueReceive(QueueHandle_t q,void*out,unsigned ticks){(void)q;if(queued_valid){*(save_request_t*)out=queued;queued_valid=false;return pdTRUE;}if(ticks==portMAX_DELAY)longjmp(done,1);return 0;}
int xTaskCreate(void(*fn)(void*),const char*n,unsigned stack,void*a,unsigned priority,void*h){(void)fn;(void)n;(void)stack;(void)a;(void)priority;(void)h;return task_fail?0:pdPASS;}
void vTaskDelay(unsigned ticks){assert(ticks==250);}
int64_t esp_timer_get_time(void){return 0;}
void test_log(const char*t,const char*f,...){(void)t;(void)f;}
const char*esp_err_to_name(int err){(void)err;return "test";}
int main(void){
 local_mode_tests();
 station_settings_t s=station_defaults(), loaded;
 writes=0;commits=0;
 assert(!station_settings_request_save(&s));
 open_error=ESP_ERR_NVS_NOT_FOUND;assert(station_settings_load(&loaded)==ESP_OK);
 open_error=ESP_FAIL;assert(station_settings_load(&loaded)==ESP_FAIL);assert(station_settings_save(&s)==ESP_FAIL);
 open_error=0;read_error=ESP_ERR_NVS_NOT_FOUND;assert(station_settings_load(&loaded)==ESP_OK);
 read_error=ESP_FAIL;assert(station_settings_load(&loaded)==ESP_FAIL);read_error=0;
 assert(station_settings_load(&loaded)==ESP_ERR_INVALID_VERSION);
 s.brightness=42;s.off_after_minutes=2;assert(station_settings_save(&s)==ESP_OK&&writes==4&&commits==1);
 assert(station_settings_load(&loaded)==ESP_OK&&loaded.brightness==42&&loaded.off_after_minutes==2);
 assert(station_settings_decode(v2,sizeof(v2),&loaded)&&loaded.brightness==42&&loaded.night_mode==0);
 legacy_only=true;assert(station_settings_load(&loaded)==ESP_OK);legacy_only=false;
 write_error=ESP_FAIL;assert(station_settings_save(&s)==ESP_FAIL);write_error=0;
 commit_error=ESP_FAIL;assert(station_settings_save(&s)==ESP_FAIL);commit_error=0;
 s.brightness=255;assert(station_settings_save(&s)==ESP_ERR_INVALID_ARG);s=station_defaults();
 queue_fail=true;assert(station_settings_worker_start()==ESP_ERR_NO_MEM);queue_fail=false;
 task_fail=true;assert(station_settings_worker_start()==ESP_ERR_NO_MEM);task_fail=false;
 assert(station_settings_worker_start()==ESP_OK);
 assert(station_settings_request_save(&s));s.brightness=35;assert(station_settings_request_save(&s));
 assert(station_settings_save_state()==1);
 if(setjmp(done)==0)settings_worker(NULL);
 assert(station_settings_save_state()==2);assert(station_settings_load(&loaded)==ESP_OK&&loaded.brightness==35);
 commit_error=ESP_FAIL;assert(station_settings_request_save(&s));
 if(setjmp(done)==0)settings_worker(NULL);
 assert(station_settings_save_state()==3&&closes>0);
 puts("PASS: settings persistence, legacy lookup, NVS failures, asynchronous save, latest request and the separate Local mode flag");
}
