#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "phone_setup.h"
#include "phone_tls.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "update_service.h"
#include "credentials.h"
#include "settings.h"
#include "freertos/semphr.h"
#include <time.h>
static char test_key[513];
static bool pairing_fail,rotate_fail;
static bool identity_changed;
bool station_identity_changed(void){bool result=identity_changed;identity_changed=false;return result;}
static unsigned rotations,rotated;
static const char *const replacement_tokens[]={"cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd","efefefefefefefefefefefefefefefef","01010101010101010101010101010101","23232323232323232323232323232323"};
esp_err_t station_pairing_rotate(char token[33]){rotations++;if(rotate_fail)return ESP_FAIL;strcpy(token,replacement_tokens[rotated++%4]);return ESP_OK;}
/* Local mode flag in NVS (real persistence is covered by tests/test_settings.c). */
static station_local_mode_t saved_local;static esp_err_t local_load_error,local_save_error;static unsigned local_saves;
esp_err_t station_local_mode_load(station_local_mode_t *m){*m=STATION_LOCAL_MODE_OFF;if(local_load_error)return local_load_error;*m=saved_local;return ESP_OK;}
esp_err_t station_local_mode_save(station_local_mode_t m){local_saves++;if(local_save_error)return local_save_error;saved_local=m;return ESP_OK;}
static int64_t renew_after;
int64_t station_identity_renew_after(void){return renew_after;}
/* The lock must never be re-entered or released twice. */
static bool lock_held,lock_fail;static unsigned lock_takes;
SemaphoreHandle_t xSemaphoreCreateMutex(void){static int lock;return lock_fail?NULL:(SemaphoreHandle_t)&lock;}
int xSemaphoreTake(SemaphoreHandle_t l,uint32_t t){assert(l&&t==portMAX_DELAY&&!lock_held);lock_held=true;lock_takes++;return pdTRUE;}
int xSemaphoreGive(SemaphoreHandle_t l){assert(l&&lock_held);lock_held=false;return pdTRUE;}
esp_err_t station_pairing_load(char token[33]){if(pairing_fail)return ESP_FAIL;strcpy(token,"abababababababababababababababab");return ESP_OK;}
esp_err_t station_credential_set(const char *key){if(strlen(key)>512)return ESP_ERR_INVALID_ARG;snprintf(test_key,sizeof(test_key),"%s",key);return ESP_OK;}
bool station_transport_refresh(void){return true;}
void station_update_status(station_update_status_t *out){memset(out,0,sizeof(*out));strcpy(out->installed,"0.9.0");out->interval_hours=720;out->credential_configured=test_key[0]!=0;}
static unsigned update_calls;
bool station_update_request(const char *a){update_calls++;return !strcmp(a,"check")||!strcmp(a,"install")||!strcmp(a,"postpone");}
bool station_update_configure(unsigned h,bool a,unsigned s,unsigned e){(void)a;update_calls++;return (h==1||h==720)&&s!=e;}
static httpd_uri_t routes[14],http_routes[14];static unsigned route_count,http_route_count,location_calls;
static station_network_status_t network;
static bool dns_match=true;static uint32_t station_address=0xac140a02;
bool station_phone_dns_matches(const char *host,uint32_t address){assert(host&&address==station_address);return dns_match;}
static int64_t now_us=1000000;
int64_t esp_timer_get_time(void){return now_us;}
static char saved_coordinates[64],saved_name[64];
struct test_queue {size_t size;unsigned count;char items[2][256];};
QueueHandle_t xQueueCreate(unsigned n,size_t size){assert(n==2&&size<=256);struct test_queue *q=calloc(1,sizeof(*q));q->size=size;return q;}
int xQueueSend(QueueHandle_t q,const void *v,unsigned ticks){assert(ticks==0);if(q->count==2)return 0;memcpy(q->items[q->count++],v,q->size);return pdTRUE;}
int xQueueReceive(QueueHandle_t q,void *v,unsigned ticks){assert(ticks==0);if(!q->count)return 0;memcpy(v,q->items[0],q->size);memmove(q->items[0],q->items[1],256);q->count--;return pdTRUE;}
void test_log(const char *t,const char *f,...){(void)t;(void)f;}
void esp_fill_random(void *v,size_t n){memset(v,0xab,n);}
void station_network_status(station_network_status_t *s){*s=network;}
int station_settings_save_state(void){return 2;}
bool station_network_home(const char *c,const char *n){location_calls++;strcpy(saved_coordinates,c);strcpy(saved_name,n);return true;}
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key){assert(!strcmp(key,"WIFI_STA_DEF"));return (void*)1;}
esp_err_t esp_netif_get_ip_info(esp_netif_t *n,esp_netif_ip_info_t *i){(void)n;i->ip.addr=station_address;return ESP_OK;}
esp_err_t httpd_resp_set_hdr(httpd_req_t *r,const char *key,const char *value){if(!strcmp(key,"Content-Disposition"))r->download=!strcmp(value,"attachment; filename=\"Weather Station Location.shortcut\"");if(!strcmp(key,"Cache-Control"))r->no_store=!strcmp(value,"no-store");if(!strcmp(key,"Referrer-Policy"))r->no_referrer=!strcmp(value,"no-referrer");return ESP_OK;}
esp_err_t httpd_resp_set_type(httpd_req_t *r,const char *t){(void)r;(void)t;return ESP_OK;}
esp_err_t httpd_resp_set_status(httpd_req_t *r,const char *s){r->code=atoi(s);return ESP_OK;}
esp_err_t httpd_resp_sendstr(httpd_req_t *r,const char *s){snprintf(r->response,sizeof(r->response),"%s",s);return ESP_OK;}
esp_err_t httpd_resp_send(httpd_req_t *r,const char *s,int n){assert(n>=0&&(size_t)n<sizeof(r->response));memcpy(r->response,s,n);r->response[n]=0;r->response_len=n;return ESP_OK;}
esp_err_t httpd_resp_send_err(httpd_req_t *r,int code,const char *s){r->code=code;return httpd_resp_sendstr(r,s);}
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r,const char *key,char *out,size_t size){const char *s=!strcmp(key,"Authorization")?r->auth:r->type;if(!s||strlen(s)>=size)return ESP_FAIL;strcpy(out,s);return ESP_OK;}
int httpd_req_recv(httpd_req_t *r,char *out,size_t n){size_t take=n>7?7:n;memcpy(out,r->body+r->received,take);r->received+=take;return take;}
const char *station_phone_tls_hostname(void){return "device-a7f3.tempo.tinyrobotcompany.io";}
static bool fail_start=true,fail_route=true,fail_stop;
static unsigned tls_starts,tls_stops;
esp_err_t station_phone_tls_stop(httpd_handle_t h){(void)h;tls_stops++;if(fail_stop)return ESP_FAIL;route_count=0;return ESP_OK;}
esp_err_t station_phone_tls_start(httpd_handle_t *h,const httpd_config_t *c){assert(c->max_open_sockets==3&&c->recv_wait_timeout==3&&c->stack_size==10240);tls_starts++;if(fail_start)return ESP_FAIL;*h=(void*)1;return ESP_OK;}
/* Port 80 server (Local mode). Handle 2; HTTPS uses handle 1. */
static bool http_start_fail,http_route_fail,http_stop_fail,http_running;static unsigned http_starts,http_stops;
esp_err_t httpd_start(httpd_handle_t *h,const httpd_config_t *c){assert(c->server_port==80&&c->ctrl_port&&c->ctrl_port!=32768&&c->max_open_sockets==2&&c->max_uri_handlers==11&&!http_running);http_starts++;if(http_start_fail)return ESP_FAIL;*h=(void*)2;http_running=true;http_route_count=0;return ESP_OK;}
esp_err_t httpd_stop(httpd_handle_t h){assert(h==(void*)2&&http_running);http_stops++;if(http_stop_fail)return ESP_FAIL;http_running=false;http_route_count=0;return ESP_OK;}
esp_err_t httpd_register_uri_handler(httpd_handle_t h,const httpd_uri_t *route){
 if(h==(void*)2){assert(http_running);if(http_route_fail&&http_route_count==5)return ESP_FAIL;assert(http_route_count<14);http_routes[http_route_count++]=*route;return ESP_OK;}
 assert(h==(void*)1);if(fail_route&&route_count==3)return ESP_FAIL;assert(route_count<14);routes[route_count++]=*route;return ESP_OK;}
static httpd_req_t serve(httpd_uri_t *table,unsigned count,const char *uri,const char *body,const char *auth){httpd_req_t r={.uri=uri,.body=body,.content_len=body?strlen(body):0,.auth=auth,.type="application/json",.code=200};for(unsigned i=0;i<count;i++)if(!strcmp(uri,table[i].uri)){assert(table[i].handler(&r)==ESP_OK);assert(!lock_held);return r;}assert(0);return r;}
static httpd_req_t request(const char *uri,const char *body,const char *auth){return serve(routes,route_count,uri,body,auth);}
static httpd_req_t plain(const char *uri,const char *body,const char *auth){assert(http_running);return serve(http_routes,http_route_count,uri,body,auth);}
static void local_mode_tests(void);
static int boot_case(const char *name);
int main(int argc,char **argv){if(argc>1)return boot_case(argv[1]);station_settings_t initial=station_defaults();pairing_fail=true;assert(station_phone_start(&initial)==ESP_FAIL);pairing_fail=false;assert(station_phone_start(&initial)==ESP_FAIL);station_phone_link_t unavailable;network.connected=true;station_phone_link(&unavailable);assert(!unavailable.online);
 fail_start=false;assert(station_phone_start(&initial)==ESP_FAIL&&route_count==0);station_phone_link(&unavailable);assert(!unavailable.online);
 fail_route=false;identity_changed=true;station_phone_maintenance();assert(route_count==11&&!identity_changed);assert(station_phone_start(&initial)==ESP_OK&&route_count==11);network.connected=false;
 httpd_req_t asset=request("/",NULL,NULL);assert(strstr(asset.response,"Your weather station"));
 asset=request("/connection.js",NULL,NULL);assert(strstr(asset.response,"StationConnection"));
 const char *auth="Bearer abababababababababababababababab";
 httpd_req_t secret=request("/api/credential","{\"key\":\"fixture-only\"}",NULL);assert(secret.code==401&&!test_key[0]);
 secret=request("/api/credential","{\"key\":\"fixture-only\"}",auth);assert(secret.code==200&&!strcmp(test_key,"fixture-only")&&!strstr(secret.response,"fixture-only"));
 secret=request("/api/credential","{\"key\":false}",auth);assert(secret.code==400);
 secret=request("/api/credential","",NULL);assert(secret.code==401&&!strcmp(test_key,"fixture-only"));
 secret=request("/api/credential","{\"key\":\"\"}","Bearer wrong");assert(secret.code==401&&!strcmp(test_key,"fixture-only"));
 static const char *const unauth_updates[]={"{\"action\":\"check\"}","{\"action\":\"install\"}","{\"action\":\"postpone\"}",
   "{\"action\":\"configure\",\"hours\":1,\"automatic\":false,\"start\":0,\"end\":60}"};
 for(unsigned i=0;i<sizeof(unauth_updates)/sizeof(unauth_updates[0]);i++){
   secret=request("/api/update",unauth_updates[i],NULL);assert(secret.code==401);
   secret=request("/api/update",unauth_updates[i],"Bearer wrong");assert(secret.code==401);
 }
 assert(update_calls==0);
 secret=request("/api/update","{\"action\":\"check\"}",auth);assert(secret.code==202);
 secret=request("/api/update","{\"action\":\"invalid\"}",auth);assert(secret.code==400);
 secret=request("/api/update","{\"action\":\"configure\",\"hours\":720,\"automatic\":true,\"start\":180,\"end\":240}",auth);assert(secret.code==202);
 secret=request("/api/update","{\"action\":\"configure\",\"hours\":1.5,\"automatic\":true,\"start\":180,\"end\":240}",auth);assert(secret.code==400);
 httpd_req_t r=request("/api/status",NULL,NULL);assert(r.code==401&&!strstr(r.response,"settings"));
 r=request("/api/status",NULL,"Bearer wrong");assert(r.code==401);
 r=request("/api/status",NULL,auth);assert(r.code==200&&r.no_store&&r.no_referrer&&strstr(r.response,"brightness")&&!strstr(r.response,"ababab"));

 assert(!strstr(r.response,"shortcutAvailable"));
 for(unsigned i=0;i<route_count;i++){assert(strcmp(routes[i].uri,"/api/location-request"));assert(strcmp(routes[i].uri,"/Weather-Station-Location.shortcut"));}
 r=request("/api/view","{\"view\":1}",auth);assert(r.code==202);station_phone_command_t cmd;assert(station_phone_take(&cmd)&&cmd.change_view&&cmd.view==VIEW_WEATHER);assert(!station_phone_take(&cmd));
 r=request("/api/view","{\"view\":1.5}",auth);assert(r.code==400);r=request("/api/view","{\"view\":99}",auth);assert(r.code==400);
 r=request("/api/view","{\"view\":1} junk",auth);assert(r.code==400);r=request("/api/view","[[[[[[[[[]]]]]]]]]",auth);assert(r.code==400);
 r=request("/api/location","{\"latitude\":48.81,\"longitude\":2.22,\"name\":\"Home suburb\"}",auth);assert(r.code==202&&location_calls==1&&!strcmp(saved_coordinates,"48.810000,2.220000")&&!strcmp(saved_name,"Home suburb"));
 r=request("/api/location","{\"latitude\":48,\"longitude\":2,\"name\":\"Other\"}",auth);assert(r.code==503&&location_calls==1);
 network.location_completion++;network.location_success=true;r=request("/api/status",NULL,auth);assert(strstr(r.response,"\"pending\":false"));
 station_phone_link_t link;station_phone_link(&link);assert(!link.online);network.connected=true;station_phone_maintenance();station_phone_link(&link);assert(link.online&&strstr(link.url,"https://device-a7f3.tempo.tinyrobotcompany.io/#token="));
 dns_match=false;station_phone_maintenance();station_phone_link(&link);assert(!link.online&&link.dns_pending);
 dns_match=true;station_phone_maintenance();station_phone_link(&link);assert(link.online);
 station_address++;station_phone_link(&link);assert(!link.online);station_phone_maintenance();station_phone_link(&link);assert(link.online);
 now_us+=10000000LL;station_phone_link(&link);assert(!link.online);station_phone_maintenance();
 station_phone_forget();station_phone_forget();station_phone_link(&link);assert(link.forget_state==1);
 rotate_fail=true;station_phone_maintenance();station_phone_link(&link);assert(link.forget_state==3);r=request("/api/status",NULL,auth);assert(r.code==200);
 rotate_fail=false;station_phone_forget();station_phone_link(&link);assert(link.forget_state==1);station_phone_forget();station_phone_maintenance();station_phone_link(&link);assert(link.forget_state==2&&strstr(link.url,"cdcd"));
 assert(rotations==2);station_phone_maintenance();assert(rotations==2);
 r=request("/api/status",NULL,auth);assert(r.code==401);r=request("/api/status",NULL,"Bearer cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd");assert(r.code==200);
 unsigned before_starts=tls_starts,before_stops=tls_stops;
 fail_stop=true;identity_changed=true;station_phone_maintenance();
 assert(tls_starts==before_starts&&tls_stops==before_stops+1&&route_count==11);
 assert(station_phone_start(&initial)!=ESP_OK&&tls_starts==before_starts);
 fail_stop=false;station_phone_maintenance();assert(tls_starts==before_starts+1&&route_count==11);
 before_starts=tls_starts;before_stops=tls_stops;
 identity_changed=true;station_phone_maintenance();assert(route_count==11&&!identity_changed);
 assert(tls_starts==before_starts+1&&tls_stops==before_stops+1);
 station_phone_maintenance();assert(tls_starts==before_starts+1&&tls_stops==before_stops+1);
 r=request("/api/status",NULL,"Bearer cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd");assert(r.code==200);
 identity_changed=true;fail_start=true;station_phone_maintenance();station_phone_link(&link);assert(!link.online);
 fail_start=false;station_phone_maintenance();station_phone_link(&link);assert(link.online&&route_count==11);
 local_mode_tests();
 puts("PASS: real HTTP handlers: auth, bounds, queued commands, TLS renewal/retry, cache headers and QR URL");}
#include "test_phone_local_mode.inc"
