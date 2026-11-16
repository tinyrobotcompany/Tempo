#include <assert.h>
#include <time.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
static time_t test_clock=1800000000;
static time_t fake_time(time_t *v){if(v)*v=test_clock;return test_clock;}
#define time fake_time
#include "../firmware/main/update_service.c"
const char test_public_key[] __asm__("_binary_ota_public_pem_start")="test public key";
static int64_t mono=1000000;
static station_network_status_t net={.connected=true,.source=TIME_NETWORK};
static esp_app_desc_t desc={.version="0.9.0",.project_name="weather_station"};
static esp_partition_t part={.size=0x800000};
static int open_error,read_error=ESP_ERR_NVS_NOT_FOUND,write_error,commit_error,crypto_error,ota_error,begin_error,boot_error,http_code=200;
static bool queue_fail,task_fail,active_pending,no_partition,truncated,read_failed,init_failed,connect_failed,has_redirect;
static unsigned activated,aborted,restarted,cleanup_count,loops,stop_loop;
static saved_t persisted;
static jmp_buf done;
struct test_queue {command_t item[8];unsigned n;} q;
static const char *response;static size_t offset,response_size;static char image[1024],manifest_text[1024];
static const char latest[]="{\"tag_name\":\"v0.9.1\",\"draft\":false,\"prerelease\":false}";
void test_log(const char *tag,const char *fmt,...){(void)tag;(void)fmt;}
int64_t esp_timer_get_time(void){return mono;}
void station_network_status(station_network_status_t *v){*v=net;}
bool station_credential_configured(void){return true;}
bool station_credential_get(char *out,size_t n){snprintf(out,n,"host-test-only");return true;}
const esp_app_desc_t *esp_app_get_description(void){return &desc;}
uint32_t esp_random(void){return 0;}
int nvs_open(const char *name,int mode,nvs_handle_t *h){assert(!strcmp(name,"tempo_ota"));(void)mode;*h=1;return open_error;}
void nvs_close(nvs_handle_t h){(void)h;}
int nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *n){(void)h;(void)key;if(read_error)return read_error;assert(*n==sizeof(persisted));memcpy(out,&persisted,*n);return 0;}
int nvs_set_blob(nvs_handle_t h,const char *key,const void *v,size_t n){(void)h;(void)key;assert(n==sizeof(persisted));if(write_error)return write_error;memcpy(&persisted,v,n);return 0;}
int nvs_commit(nvs_handle_t h){(void)h;return commit_error;}
QueueHandle_t xQueueCreate(unsigned count,size_t size){assert(count==4&&size==sizeof(command_t));return queue_fail?NULL:&q;}
int xQueueSend(QueueHandle_t handle,const void *v,unsigned ticks){(void)handle;(void)ticks;if(q.n==8)return 0;q.item[q.n++]=*(const command_t *)v;return pdTRUE;}
int xQueueReceive(QueueHandle_t handle,void *v,unsigned ticks){(void)handle;(void)ticks;if(!q.n)return 0;*(command_t *)v=q.item[0];memmove(q.item,q.item+1,--q.n*sizeof(command_t));return pdTRUE;}
void vQueueDelete(QueueHandle_t h){(void)h;}
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle){(void)fn;(void)arg;(void)handle;assert(!strcmp(name,"ota")&&stack==16384&&priority==1);return task_fail?0:pdPASS;}
void vTaskDelay(unsigned ticks){mono+=(int64_t)ticks*1000;test_clock+=ticks/1000;if(stop_loop&&++loops>=stop_loop)longjmp(done,1);}
const esp_partition_t *esp_ota_get_running_partition(void){return &part;}
const esp_partition_t *esp_ota_get_next_update_partition(const void *p){(void)p;return no_partition?NULL:&part;}
int esp_ota_get_state_partition(const esp_partition_t *p,esp_ota_img_states_t *s){(void)p;*s=active_pending?ESP_OTA_IMG_PENDING_VERIFY:ESP_OTA_IMG_VALID;return 0;}
int esp_ota_begin(const esp_partition_t *p,unsigned size,esp_ota_handle_t *h){(void)p;assert(size==1024);*h=1;return begin_error;}
int esp_ota_write(esp_ota_handle_t h,const void *data,size_t n){assert(h==1&&data&&n>0);return ota_error;}
int esp_ota_end(esp_ota_handle_t h){assert(h==1);return ota_error;}
int esp_ota_abort(esp_ota_handle_t h){assert(h==1);aborted++;return 0;}
int esp_ota_get_partition_description(const esp_partition_t *p,esp_app_desc_t *d){(void)p;*d=desc;strcpy(d->version,"0.9.1");return 0;}
int esp_ota_set_boot_partition(const esp_partition_t *p){(void)p;if(!boot_error)activated++;return boot_error;}
void esp_restart(void){restarted++;}
void esp_crt_bundle_attach(void){}
void *esp_http_client_init(esp_http_client_config_t *c){assert(c->crt_bundle_attach&&c->disable_auto_redirect);offset=0;
 if(has_redirect&&strstr(c->url,"github.com")){esp_http_client_event_t e={.event_id=HTTP_EVENT_ON_HEADER,.header_key="Location",.header_value="https://release-assets.githubusercontent.com/test",.user_data=c->user_data};c->event_handler(&e);}
 response=strstr(c->url,"/latest")?latest:strstr(c->url,"manifest.json")?manifest_text:strstr(c->url,"manifest.sig")?"signature":image;
 response_size=response==image?sizeof(image):strlen(response);return init_failed?NULL:(void *)1;}
int esp_http_client_open(void *h,int n){(void)h;(void)n;return connect_failed?-1:0;}
int esp_http_client_set_header(void *h,const char *key,const char *value){(void)h;(void)value;assert(strcmp(key,"apikey"));return 0;}
int64_t esp_http_client_fetch_headers(void *h){(void)h;return response_size;}
int esp_http_client_get_status_code(void *h){(void)h;if(has_redirect){has_redirect=false;return 302;}return http_code;}
void esp_http_client_cleanup(void *h){(void)h;cleanup_count++;}
int esp_http_client_read(void *h,char *out,int n){(void)h;if(read_failed)return -1;size_t take=response_size-offset;if(take>(size_t)n)take=(size_t)n;memcpy(out,response+offset,take);offset+=take;return (int)take;}
bool esp_http_client_is_complete_data_received(void *h){(void)h;return !truncated&&offset==response_size;}
void mbedtls_md_init(mbedtls_md_context_t *c){(void)c;}void mbedtls_md_free(mbedtls_md_context_t *c){(void)c;}
void *mbedtls_md_info_from_type(int t){assert(t==MBEDTLS_MD_SHA256);return (void *)1;}
int mbedtls_md_setup(mbedtls_md_context_t *c,const void *t,int n){(void)c;(void)t;(void)n;return crypto_error;}
int mbedtls_md_starts(mbedtls_md_context_t *c){(void)c;return 0;}
int mbedtls_md_update(mbedtls_md_context_t *c,const void *p,size_t n){(void)c;(void)p;(void)n;return 0;}
int mbedtls_md_finish(mbedtls_md_context_t *c,unsigned char *out){(void)c;memset(out,0,32);return 0;}
int mbedtls_md(const void *t,const unsigned char *p,size_t n,unsigned char *out){(void)t;(void)p;(void)n;memset(out,0,32);return crypto_error;}
void mbedtls_pk_init(mbedtls_pk_context *c){(void)c;}void mbedtls_pk_free(mbedtls_pk_context *c){(void)c;}
int mbedtls_pk_parse_public_key(mbedtls_pk_context *c,const unsigned char *p,size_t n){(void)c;assert(p&&n>0);return crypto_error;}
int mbedtls_pk_verify(mbedtls_pk_context *c,int t,const unsigned char *p,size_t n,const unsigned char *s,size_t sn){(void)c;(void)t;(void)p;(void)n;(void)s;(void)sn;return crypto_error;}
static void run(unsigned count){loops=0;stop_loop=count;if(!setjmp(done))worker(NULL);stop_loop=0;}
int main(void){
 snprintf(manifest_text,sizeof(manifest_text),"{\"schema\":1,\"version\":\"0.9.1\",\"target\":\"%s\",\"layout\":\"%s\",\"size\":1024,\"sha256\":\"%064d\",\"url\":\"%sv0.9.1/tempo.bin\"}",TEMPO_UPDATE_TARGET,TEMPO_UPDATE_LAYOUT,0,TEMPO_UPDATE_BASE);
 assert(!station_update_request("check"));assert(!station_update_configure(720,true,180,240));
 open_error=-1;assert(station_update_start()!=0);open_error=0;read_error=-1;assert(station_update_start()!=0);read_error=ESP_ERR_NVS_NOT_FOUND;
 queue_fail=true;assert(station_update_start()!=0);queue_fail=false;task_fail=true;assert(station_update_start()!=0);task_fail=false;assert(station_update_start()==0);assert(station_update_start()==0);
 station_update_status_t s;station_update_status(&s);assert(s.interval_hours==720&&s.automatic&&s.credential_configured);
 assert(!station_update_request(NULL));assert(!station_update_request("bad"));assert(!station_update_configure(2,true,180,240));assert(!station_update_configure(1,true,180,180));
 assert(station_update_request("check"));run(1);assert(status.state==UPDATE_AVAILABLE&&saved.next>test_clock);assert(candidate.size==1024);
 assert(station_update_configure(1,false,10,20));run(1);assert(saved.hours==1&&!saved.automatic);
 assert(station_update_request("postpone"));run(1);assert(saved.postponed>test_clock);
 net.connected=false;assert(station_update_request("check"));run(1);assert(status.state==UPDATE_ERROR);assert(station_update_request("install"));run(1);net.connected=true;
 http_code=404;assert(check_release());http_code=500;assert(!check_release());http_code=200;
 crypto_error=-1;assert(!check_release());crypto_error=0;
 strcpy(saved.blocked,"0.9.1");assert(check_release());saved.blocked[0]=0;strcpy(status.installed,"0.9.1");assert(check_release());strcpy(status.installed,"0.9.0");
 truncated=true;assert(!check_release());truncated=false;read_failed=true;assert(!check_release());read_failed=false;
 init_failed=true;assert(!check_release());init_failed=false;connect_failed=true;assert(!check_release());connect_failed=false;assert(check_release());
 int http;assert(!open_url("http://github.com/evil",&http));has_redirect=true;void *h=open_url("https://github.com/test",&http);assert(h);esp_http_client_cleanup(h);
 char ver[32];strcpy(ver,candidate.version);candidate.version[0]=0;assert(!install());strcpy(candidate.version,ver);
 active_pending=true;assert(!install());active_pending=false;no_partition=true;assert(!install());no_partition=false;
 http_code=500;assert(!install());http_code=200;begin_error=-1;assert(!install());begin_error=0;
 ota_error=-1;assert(!install()&&aborted);ota_error=0;truncated=true;assert(!install());truncated=false;
 candidate.sha256[0]='1';assert(!install());candidate.sha256[0]='0';commit_error=-1;assert(!install());commit_error=0;boot_error=-1;assert(!install());boot_error=0;
 assert(activated==0);assert(install()&&activated==1&&restarted==1);
 assert(station_update_request("install"));run(63);assert(restarted==2);
 commands=NULL;read_error=0;persisted=saved;strcpy(persisted.pending,"0.9.1");assert(station_update_start()==0);assert(!strcmp(saved.blocked,"0.9.1"));
 commands=NULL;persisted.schema=2;assert(station_update_start()!=0);persisted.schema=1;write_error=-1;strcpy(persisted.pending,"0.9.1");assert(station_update_start()!=0);write_error=0;
 puts("PASS: actual OTA worker HTTP limits, signature rejection, scheduling, persistent rollback block and inactive-slot activation only after validation");
}
