#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "phone_setup.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "update_service.h"
#include "credentials.h"
static char test_key[513];
static bool pairing_fail;
esp_err_t station_pairing_load(char token[33]){if(pairing_fail)return ESP_FAIL;strcpy(token,"abababababababababababababababab");return ESP_OK;}
esp_err_t station_credential_set(const char *key){if(strlen(key)>512)return ESP_ERR_INVALID_ARG;snprintf(test_key,sizeof(test_key),"%s",key);return ESP_OK;}
bool station_transport_refresh(void){return true;}
void station_update_status(station_update_status_t *out){memset(out,0,sizeof(*out));strcpy(out->installed,"0.9.0");out->interval_hours=720;out->credential_configured=test_key[0]!=0;}
static unsigned update_calls;
bool station_update_request(const char *a){update_calls++;return !strcmp(a,"check")||!strcmp(a,"install")||!strcmp(a,"postpone");}
bool station_update_configure(unsigned h,bool a,unsigned s,unsigned e){(void)a;update_calls++;return (h==1||h==720)&&s!=e;}
static httpd_uri_t routes[14];static unsigned route_count,location_calls;
static station_network_status_t network;
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
esp_err_t esp_netif_get_ip_info(esp_netif_t *n,esp_netif_ip_info_t *i){(void)n;i->ip.addr=0xac140a02;return ESP_OK;}
esp_err_t httpd_resp_set_hdr(httpd_req_t *r,const char *key,const char *value){if(!strcmp(key,"Content-Disposition"))r->download=!strcmp(value,"attachment; filename=\"Weather Station Location.shortcut\"");if(!strcmp(key,"Cache-Control"))r->no_store=!strcmp(value,"no-store");if(!strcmp(key,"Referrer-Policy"))r->no_referrer=!strcmp(value,"no-referrer");return ESP_OK;}
esp_err_t httpd_resp_set_type(httpd_req_t *r,const char *t){(void)r;(void)t;return ESP_OK;}
esp_err_t httpd_resp_set_status(httpd_req_t *r,const char *s){r->code=atoi(s);return ESP_OK;}
esp_err_t httpd_resp_sendstr(httpd_req_t *r,const char *s){snprintf(r->response,sizeof(r->response),"%s",s);return ESP_OK;}
esp_err_t httpd_resp_send(httpd_req_t *r,const char *s,int n){assert(n>=0&&(size_t)n<sizeof(r->response));memcpy(r->response,s,n);r->response[n]=0;r->response_len=n;return ESP_OK;}
esp_err_t httpd_resp_send_err(httpd_req_t *r,int code,const char *s){r->code=code;return httpd_resp_sendstr(r,s);}
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r,const char *key,char *out,size_t size){const char *s=!strcmp(key,"Authorization")?r->auth:r->type;if(!s||strlen(s)>=size)return ESP_FAIL;strcpy(out,s);return ESP_OK;}
int httpd_req_recv(httpd_req_t *r,char *out,size_t n){size_t take=n>7?7:n;memcpy(out,r->body+r->received,take);r->received+=take;return take;}
static bool fail_start=true,fail_route=true;
esp_err_t httpd_stop(httpd_handle_t h){(void)h;route_count=0;return ESP_OK;}
esp_err_t httpd_start(httpd_handle_t *h,const httpd_config_t *c){assert(c->max_open_sockets==3&&c->recv_wait_timeout==3&&c->stack_size==8192);if(fail_start)return ESP_FAIL;*h=(void*)1;return ESP_OK;}
esp_err_t httpd_register_uri_handler(httpd_handle_t h,const httpd_uri_t *route){(void)h;if(fail_route&&route_count==3)return ESP_FAIL;assert(route_count<14);routes[route_count++]=*route;return ESP_OK;}
static httpd_req_t request(const char *uri,const char *body,const char *auth){httpd_req_t r={.uri=uri,.body=body,.content_len=body?strlen(body):0,.auth=auth,.type="application/json",.code=200};for(unsigned i=0;i<route_count;i++)if(!strcmp(uri,routes[i].uri)){assert(routes[i].handler(&r)==ESP_OK);return r;}assert(0);return r;}
int main(void){station_settings_t initial=station_defaults();pairing_fail=true;assert(station_phone_start(&initial)==ESP_FAIL);pairing_fail=false;assert(station_phone_start(&initial)==ESP_FAIL);station_phone_link_t unavailable;network.connected=true;station_phone_link(&unavailable);assert(!unavailable.online);
 fail_start=false;assert(station_phone_start(&initial)==ESP_FAIL&&route_count==0);station_phone_link(&unavailable);assert(!unavailable.online);
 fail_route=false;assert(station_phone_start(&initial)==ESP_OK&&route_count==14);assert(station_phone_start(&initial)==ESP_OK&&route_count==14);network.connected=false;
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

#ifdef STATION_HAS_LOCATION_SHORTCUT
 assert(strstr(r.response,"\"shortcutAvailable\":true"));
 r=request("/Weather-Station-Location.shortcut",NULL,NULL);
 extern const char installer_start[] __asm__("_binary_Weather_Station_Location_shortcut_start");
 extern const char installer_end[] __asm__("_binary_Weather_Station_Location_shortcut_end");
 assert(r.code==200&&r.download&&r.no_store&&r.response_len==(size_t)(installer_end-installer_start));
 assert(!memcmp(r.response,installer_start,r.response_len)&&!memcmp(r.response,"AEA1",4));
#else
 assert(strstr(r.response,"\"shortcutAvailable\":false"));
 r=request("/Weather-Station-Location.shortcut",NULL,NULL);assert(r.code==503);
#endif
 r=request("/api/location-request","{\"action\":\"begin\"}",NULL);assert(r.code==401);
 r=request("/api/location-request","{\"action\":\"validate\",\"state\":\"abababababababababababababababab\"}",auth);assert(r.code==409);
 r=request("/api/location-request","{\"action\":\"begin\"}",auth);assert(r.code==200&&strstr(r.response,"abababababababababababababababab"));
 // A new browser/tab has only the callback token and nonce, not sessionStorage.
 r=request("/api/location-request","{\"action\":\"validate\",\"state\":\"abababababababababababababababab\"}",auth);assert(r.code==200&&location_calls==0);
 r=request("/api/location-request","{\"action\":\"validate\",\"state\":\"abababababababababababababababab\"}",auth);assert(r.code==200&&location_calls==0);
 r=request("/api/location-request","{\"action\":\"validate\",\"state\":\"00000000000000000000000000000000\"}",auth);assert(r.code==409);
 now_us+=600000000LL;
 r=request("/api/location-request","{\"action\":\"validate\",\"state\":\"abababababababababababababababab\"}",auth);assert(r.code==409);
 r=request("/api/view","{\"view\":1}",auth);assert(r.code==202);station_phone_command_t cmd;assert(station_phone_take(&cmd)&&cmd.change_view&&cmd.view==VIEW_WEATHER);assert(!station_phone_take(&cmd));
 r=request("/api/view","{\"view\":1.5}",auth);assert(r.code==400);r=request("/api/view","{\"view\":99}",auth);assert(r.code==400);
 r=request("/api/view","{\"view\":1} junk",auth);assert(r.code==400);r=request("/api/view","[[[[[[[[[]]]]]]]]]",auth);assert(r.code==400);
 r=request("/api/location","{\"latitude\":48.81,\"longitude\":2.22,\"name\":\"Home suburb\"}",auth);assert(r.code==202&&location_calls==1&&!strcmp(saved_coordinates,"48.810000,2.220000")&&!strcmp(saved_name,"Home suburb"));
 r=request("/api/location","{\"latitude\":48,\"longitude\":2,\"name\":\"Other\"}",auth);assert(r.code==503&&location_calls==1);
 network.location_completion++;network.location_success=true;r=request("/api/status",NULL,auth);assert(strstr(r.response,"\"pending\":false"));
 station_phone_link_t link;station_phone_link(&link);assert(!link.online);network.connected=true;station_phone_link(&link);assert(link.online&&strstr(link.url,"172.20.10.2/#token="));
 puts("PASS: real HTTP handlers: auth, bounds, queued commands, busy location, cache headers and hotspot QR URL");}
