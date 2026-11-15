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
static int open_error, read_error, write_error, commit_error;
static bool legacy_only, queue_fail, task_fail;
static unsigned closes, commits, writes;
static save_request_t queued;
static bool queued_valid;
static jmp_buf done;
struct test_queue { int unused; } queue;
int nvs_open(const char*n,int mode,nvs_handle_t*h){assert(!strcmp(n,"station"));(void)mode;*h=1;return open_error;}
void nvs_close(nvs_handle_t h){assert(h==1);closes++;}
int nvs_get_blob(nvs_handle_t h,const char*k,void*out,size_t*n){(void)h;if(legacy_only&&!strcmp(k,"settings_v2"))return ESP_ERR_NVS_NOT_FOUND;if(read_error)return read_error;assert(*n>=blob_size);memcpy(out,blob,blob_size);*n=blob_size;return ESP_OK;}
int nvs_set_blob(nvs_handle_t h,const char*k,const void*in,size_t n){(void)h;writes++;if(write_error)return write_error;if(!strcmp(k,"settings_v2")){memcpy(blob,in,n);blob_size=n;}else assert(!strcmp(k,"settings")&&n==6);return ESP_OK;}
int nvs_commit(nvs_handle_t h){(void)h;commits++;return commit_error;}
QueueHandle_t xQueueCreate(unsigned n,size_t size){assert(n==1&&size==sizeof(save_request_t));return queue_fail?NULL:&queue;}
int xQueueOverwrite(QueueHandle_t q,const void*item){assert(q==&queue);queued=*(const save_request_t*)item;queued_valid=true;return pdTRUE;}
int xQueueReceive(QueueHandle_t q,void*out,unsigned ticks){(void)q;if(queued_valid){*(save_request_t*)out=queued;queued_valid=false;return pdTRUE;}if(ticks==portMAX_DELAY)longjmp(done,1);return 0;}
int xTaskCreate(void(*fn)(void*),const char*n,unsigned stack,void*a,unsigned priority,void*h){(void)fn;(void)n;(void)stack;(void)a;(void)priority;(void)h;return task_fail?0:pdPASS;}
void vTaskDelay(unsigned ticks){assert(ticks==250);}
int64_t esp_timer_get_time(void){return 0;}
void test_log(const char*t,const char*f,...){(void)t;(void)f;}
const char*esp_err_to_name(int err){(void)err;return "test";}
int main(void){
 station_settings_t s=station_defaults(), loaded;
 assert(!station_settings_request_save(&s));
 open_error=ESP_ERR_NVS_NOT_FOUND;assert(station_settings_load(&loaded)==ESP_OK);
 open_error=ESP_FAIL;assert(station_settings_load(&loaded)==ESP_FAIL);assert(station_settings_save(&s)==ESP_FAIL);
 open_error=0;read_error=ESP_ERR_NVS_NOT_FOUND;assert(station_settings_load(&loaded)==ESP_OK);
 read_error=ESP_FAIL;assert(station_settings_load(&loaded)==ESP_FAIL);read_error=0;
 assert(station_settings_load(&loaded)==ESP_ERR_INVALID_VERSION);
 s.brightness=42;assert(station_settings_save(&s)==ESP_OK&&writes==2&&commits==1);
 assert(station_settings_load(&loaded)==ESP_OK&&loaded.brightness==42);
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
 puts("PASS: settings persistence, legacy lookup, NVS failures, asynchronous save and latest request");
}
