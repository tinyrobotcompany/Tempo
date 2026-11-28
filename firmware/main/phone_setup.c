#include "phone_setup.h"
#include "pairing.h"
#include "phone_tls.h"
#include "phone_dns.h"
#include "device_identity.h"
#include "phone_protocol.h"
#include "network_time.h"
#include "settings.h"
#include "credentials.h"
#include "transport_service.h"
#include "update_service.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
#include <math.h> /* floor/isfinite validate numeric settings before conversion. */
#include <time.h>
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static station_settings_t current_settings;
static station_view_t current_view;
static QueueHandle_t queue;
static httpd_handle_t server;
static bool server_ready;
static bool tls_reload_pending;
static char token[33];
/* UI requests; main worker performs durable writes without holding the display lock. */
static atomic_int forget_state;
static uint32_t dns_address;
static int64_t dns_verified_until;
/* Local mode: plain HTTP on port 80, chosen only on the display (station_phone_local_mode).
   http_server exists only while Local mode is on (or while a failed stop is retried). */
static httpd_handle_t http_server;
static bool http_ready,local_failed,local_rotated;
static station_local_mode_t local_mode; /* Written by the maintenance task under mux. */
static atomic_int local_request;
static esp_err_t tls_error=ESP_OK;
/* Ports 80 and 443 are separate server tasks: keep stateful API handlers serial. */
static SemaphoreHandle_t handler_lock;
static void http_open(void);
static esp_err_t http_close(void);
static void local_maintenance(void)
{
    int request=atomic_exchange(&local_request,STATION_LOCAL_QUERY);
    station_local_mode_t next=local_mode;
    if(request==STATION_LOCAL_REQUEST_ON&&local_mode==STATION_LOCAL_MODE_OFF)next=STATION_LOCAL_MODE_ON;
    /* Off is saved as ROTATE first: the token went over HTTP, so it is replaced even after a power cut. */
    if(request==STATION_LOCAL_REQUEST_OFF&&local_mode==STATION_LOCAL_MODE_ON)next=STATION_LOCAL_MODE_ROTATE;
    if(next!=local_mode){
        bool saved=station_local_mode_save(next)==ESP_OK;
        portENTER_CRITICAL(&mux);local_failed=!saved;if(saved)local_mode=next;portEXIT_CRITICAL(&mux);
        if(saved){local_rotated=false;ESP_LOGW("phone","Local mode %s on the display",next==STATION_LOCAL_MODE_ON?"turned on":"turned off");}
    }
    if(local_mode==STATION_LOCAL_MODE_ON){http_open();return;}
    if(http_close()!=ESP_OK)return; /* Port 80 must be closed before the new token exists. */
    if(local_mode!=STATION_LOCAL_MODE_ROTATE)return;
    if(!local_rotated){
        char replacement[33];
        if(station_pairing_rotate(replacement)!=ESP_OK)return;
        portENTER_CRITICAL(&mux);memcpy(token,replacement,sizeof(token));portEXIT_CRITICAL(&mux);
        memset(replacement,0,sizeof(replacement));local_rotated=true;
    }
    if(station_local_mode_save(STATION_LOCAL_MODE_OFF)!=ESP_OK)return;
    portENTER_CRITICAL(&mux);local_mode=STATION_LOCAL_MODE_OFF;portEXIT_CRITICAL(&mux);
    local_rotated=false;
}
/* Renewal starts at the certificate's midpoint and success moves the date forward.
   A clock before 2024 is not synchronised yet, so nothing is offered. */
static unsigned certificate_offers(void)
{
    int64_t due=station_identity_renew_after(),expires=station_identity_expires_at(),now=time(NULL);
    if(now<1704067200)return 0;
    return (expires>0&&now>=expires?STATION_LOCAL_OFFER_EXPIRED:0)|(due>0&&now-due>=7*86400?STATION_LOCAL_OFFER_RENEWAL:0);
}
static void check_phone_dns(void)
{
    portENTER_CRITICAL(&mux);bool ready=server_ready;portEXIT_CRITICAL(&mux);
    station_network_status_t net;station_network_status(&net);
    esp_netif_t *iface=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");esp_netif_ip_info_t info;
    uint32_t address=0;
    if(ready&&net.connected&&iface&&esp_netif_get_ip_info(iface,&info)==ESP_OK&&info.ip.addr&&
       station_phone_dns_matches(station_phone_tls_hostname(),info.ip.addr))address=info.ip.addr;
    portENTER_CRITICAL(&mux);dns_address=address;dns_verified_until=esp_timer_get_time()+10000000LL;portEXIT_CRITICAL(&mux);
}
void station_phone_forget(void)
{
    int current=atomic_load(&forget_state);
    if(current!=1)atomic_compare_exchange_strong(&forget_state,&current,1);
}
void station_phone_maintenance(void)
{
    if(queue)local_maintenance();
    if(station_identity_changed())tls_reload_pending=true;
    if(tls_reload_pending&&server){
        portENTER_CRITICAL(&mux);server_ready=false;dns_verified_until=0;portEXIT_CRITICAL(&mux);
        if(station_phone_tls_stop(server)!=ESP_OK)return;
        server=NULL;tls_reload_pending=false;
        station_settings_t snapshot;
        portENTER_CRITICAL(&mux);snapshot=current_settings;portEXIT_CRITICAL(&mux);
        /* Existing phone fetch/reconnect logic retries after this brief TLS restart. */
        station_phone_start(&snapshot);
    }
    if(!server&&queue){
        station_settings_t snapshot;
        portENTER_CRITICAL(&mux);snapshot=current_settings;portEXIT_CRITICAL(&mux);
        station_phone_start(&snapshot);
    }
    check_phone_dns();
    if(atomic_load(&forget_state)!=1)return;
    char replacement[33];
    if(station_pairing_rotate(replacement)!=ESP_OK){atomic_store(&forget_state,3);return;}
    portENTER_CRITICAL(&mux);memcpy(token,replacement,sizeof(token));portEXIT_CRITICAL(&mux);
    memset(replacement,0,sizeof(replacement));atomic_store(&forget_state,2);
}
/* HTTP handlers execute serially. Location saving remains device-owned. */
static bool location_busy;
static unsigned location_completion;
extern const char connection_js_start[] __asm__("_binary_connection_js_start");
extern const char connection_js_end[] __asm__("_binary_connection_js_end");
extern const char page_start[] __asm__("_binary_index_html_start");
extern const char page_end[] __asm__("_binary_index_html_end");
extern const char app_start[] __asm__("_binary_app_js_start");
extern const char app_end[] __asm__("_binary_app_js_end");
extern const char style_start[] __asm__("_binary_style_css_start");
extern const char style_end[] __asm__("_binary_style_css_end");
extern const char location_start[] __asm__("_binary_location_js_start");
extern const char location_end[] __asm__("_binary_location_js_end");
void station_phone_publish(const station_settings_t *settings,station_view_t view)
{
    portENTER_CRITICAL(&mux);current_settings=*settings;current_view=view;portEXIT_CRITICAL(&mux);
}
bool station_phone_take(station_phone_command_t *command) {return queue&&xQueueReceive(queue,command,0)==pdTRUE;}
void station_phone_link(station_phone_link_t *link)
{
    memset(link,0,sizeof(*link));link->forget_state=atomic_load(&forget_state);
    station_network_status_t net;station_network_status(&net);
    esp_netif_t *iface=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");esp_netif_ip_info_t info;
    portENTER_CRITICAL(&mux);bool ready=server_ready;portEXIT_CRITICAL(&mux);
    bool ip_ready=iface&&esp_netif_get_ip_info(iface,&info)==ESP_OK&&info.ip.addr;
    portENTER_CRITICAL(&mux);
    bool dns_ready=ip_ready&&dns_address==info.ip.addr&&esp_timer_get_time()<dns_verified_until;
    station_local_mode_t mode=local_mode;bool http=http_ready;
    portEXIT_CRITICAL(&mux);
    if(mode==STATION_LOCAL_MODE_ON){
        /* HTTPS keeps running, but the QR offers the address that works without DNS. */
        if(http&&net.connected&&ip_ready){
            link->online=true;
            portENTER_CRITICAL(&mux);
            snprintf(link->url,sizeof(link->url),"http://" IPSTR "/#token=%s",IP2STR(&info.ip),token);
            portEXIT_CRITICAL(&mux);
        }
        return;
    }
    link->dns_pending=ready&&net.connected&&ip_ready&&!dns_ready;
    int reason=!net.connected?1:!ready?2:!ip_ready?3:!dns_ready?4:0;
    static int previous=-1;
    if(reason!=previous){ESP_LOGI("phone","Setup availability: %s",reason==1?"waiting for Wi-Fi":reason==2?"waiting for HTTPS identity/server":reason==3?"waiting for IP address":reason==4?"waiting for matching DNS":"ready");previous=reason;}
    /* While the token sent over HTTP is being replaced, show no QR at all. */
    if(ready&&net.connected&&ip_ready&&dns_ready&&mode==STATION_LOCAL_MODE_OFF) {
        link->online=true;
        portENTER_CRITICAL(&mux);
        snprintf(link->url,sizeof(link->url),"https://%s/#token=%s",station_phone_tls_hostname(),token);
        portEXIT_CRITICAL(&mux);
    }
}
static void headers(httpd_req_t *r)
{
    httpd_resp_set_hdr(r,"Strict-Transport-Security","max-age=31536000");
    httpd_resp_set_hdr(r,"Cache-Control","no-store");
    httpd_resp_set_hdr(r,"Referrer-Policy","no-referrer");
    httpd_resp_set_hdr(r,"X-Content-Type-Options","nosniff");
    httpd_resp_set_hdr(r,"X-Frame-Options","DENY");
    httpd_resp_set_hdr(r,"Content-Security-Policy","default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' https://tile.openstreetmap.org; connect-src 'self' https://data.geopf.fr; frame-ancestors 'none'; base-uri 'none'; form-action 'none'");
}
static esp_err_t json_reply(httpd_req_t *r,cJSON *j)
{
    char *text=cJSON_PrintUnformatted(j);cJSON_Delete(j);
    if(!text)return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"Out of memory");
    httpd_resp_set_type(r,"application/json");esp_err_t err=httpd_resp_sendstr(r,text);cJSON_free(text);return err;
}
static bool authorised(httpd_req_t *r)
{
    char bearer[48],expected[48];portENTER_CRITICAL(&mux);snprintf(expected,sizeof(expected),"Bearer %s",token);portEXIT_CRITICAL(&mux);
    if(httpd_req_get_hdr_value_str(r,"Authorization",bearer,sizeof(bearer))!=ESP_OK)return false;
    if(strlen(bearer)!=strlen(expected))return false;
    unsigned diff=0;for(size_t i=0;i<strlen(expected);i++)diff|=(unsigned char)bearer[i]^(unsigned char)expected[i];
    return !diff;
}
static esp_err_t asset(httpd_req_t *r)
{
    headers(r);const char *start=page_start,*end=page_end,*type="text/html; charset=utf-8";
    if(!strcmp(r->uri,"/connection.js")){start=connection_js_start;end=connection_js_end;type="text/javascript";}
    if(!strcmp(r->uri,"/app.js")){start=app_start;end=app_end;type="text/javascript";}
    if(!strcmp(r->uri,"/location.js")){start=location_start;end=location_end;type="text/javascript";}
    if(!strcmp(r->uri,"/style.css")){start=style_start;end=style_end;type="text/css";}
    httpd_resp_set_type(r,type);return httpd_resp_send(r,start,end-start-1);
}
static esp_err_t status_get(httpd_req_t *r)
{
    headers(r);if(!authorised(r))return httpd_resp_send_err(r,HTTPD_401_UNAUTHORIZED,"Scan the QR code on your display.");
    station_network_status_t net;station_network_status(&net);station_settings_t s;station_view_t v;
    portENTER_CRITICAL(&mux);s=current_settings;v=current_view;portEXIT_CRITICAL(&mux);
    if(location_busy&&net.location_completion!=location_completion)location_busy=false;
    cJSON *j=cJSON_CreateObject(),*settings=cJSON_AddObjectToObject(j,"settings"),*loc=cJSON_AddObjectToObject(j,"location");
    if(!j||!settings||!loc){cJSON_Delete(j);return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"Out of memory");}
    cJSON_AddStringToObject(settings,"name",s.user_name);cJSON_AddNumberToObject(settings,"brightness",s.brightness);
    cJSON_AddNumberToObject(settings,"theme",s.theme);cJSON_AddNumberToObject(settings,"color",s.custom_color);
    cJSON_AddNumberToObject(settings,"font",s.clock_font);cJSON_AddBoolToObject(settings,"rotate",s.rotate);cJSON_AddNumberToObject(settings,"dwell",s.dwell_seconds);
    cJSON_AddNumberToObject(settings,"dimAfter",s.dim_after_minutes);cJSON_AddNumberToObject(settings,"offAfter",s.off_after_minutes);
    cJSON_AddNumberToObject(settings,"dimBrightness",s.night_brightness);
    cJSON_AddNumberToObject(j,"view",v);cJSON_AddBoolToObject(j,"connected",net.connected);
    cJSON_AddNumberToObject(j,"saveState",station_settings_save_state());

    cJSON_AddBoolToObject(loc,"valid",net.location_valid);cJSON_AddNumberToObject(loc,"latitude",net.latitude);cJSON_AddNumberToObject(loc,"longitude",net.longitude);
    cJSON_AddStringToObject(loc,"name",net.location_name);cJSON_AddStringToObject(loc,"timezone",net.timezone);
    cJSON_AddStringToObject(loc,"message",net.location_message);cJSON_AddNumberToObject(loc,"completion",net.location_completion);
    cJSON_AddBoolToObject(loc,"success",net.location_success);cJSON_AddBoolToObject(loc,"pending",location_busy);
    station_update_status_t u;station_update_status(&u);cJSON *update=cJSON_AddObjectToObject(j,"update");
    cJSON_AddStringToObject(update,"installed",u.installed);cJSON_AddStringToObject(update,"available",u.available);
    cJSON_AddStringToObject(update,"message",u.message);cJSON_AddStringToObject(update,"notes",u.notes_url);
    cJSON_AddBoolToObject(update,"upToDate",u.up_to_date);
    cJSON_AddNumberToObject(update,"countdown",u.countdown_seconds);cJSON_AddNumberToObject(update,"state",u.state);cJSON_AddNumberToObject(update,"progress",u.progress);
    cJSON_AddNumberToObject(update,"hours",u.interval_hours);cJSON_AddBoolToObject(update,"automatic",u.automatic);
    cJSON_AddNumberToObject(update,"start",u.window_start);cJSON_AddNumberToObject(update,"end",u.window_end);
    cJSON_AddNumberToObject(update,"lastCheck",(double)u.last_check);cJSON_AddNumberToObject(update,"nextCheck",(double)u.next_check);
    cJSON_AddBoolToObject(j,"transportConfigured",u.credential_configured);
    portENTER_CRITICAL(&mux);bool local=local_mode==STATION_LOCAL_MODE_ON;portEXIT_CRITICAL(&mux);
    cJSON_AddBoolToObject(j,"localMode",local);
    return json_reply(r,j);
}
static esp_err_t post(httpd_req_t *r)
{
    /* Single authentication gate for every POST route: /api/credential, /api/update,
       /api/location, /api/settings and /api/view all require the
       pairing bearer token before the body is read or any route is dispatched.
       There are no unauthenticated POST endpoints; tests/test_phone_server.c pins this. */
    headers(r);
    if(!authorised(r))return httpd_resp_send_err(r,HTTPD_401_UNAUTHORIZED,"Scan the QR code again.");
    char type[64];if(httpd_req_get_hdr_value_str(r,"Content-Type",type,sizeof(type))!=ESP_OK||strcmp(type,"application/json"))return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"JSON required");
    if(r->content_len==0||r->content_len>1024)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid request size");
    char body[1025];size_t used=0;
    while(used<r->content_len){int n=httpd_req_recv(r,body+used,r->content_len-used);if(n<=0)return ESP_FAIL;used+=n;}
    if(memchr(body,0,used))return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid JSON");
    body[used]=0;
    cJSON *j=station_phone_json_shallow(body)?cJSON_ParseWithOpts(body,NULL,true):NULL;
    /* Authorised: only reachable after the authorised() gate at the top of post(). */
    if(!strcmp(r->uri,"/api/credential")) {
        cJSON *key=cJSON_GetObjectItemCaseSensitive(j,"key");
        bool ok=cJSON_IsString(key)&&strlen(key->valuestring)<=STATION_PRIM_KEY_MAX;
        esp_err_t result=ok?station_credential_set(key->valuestring):ESP_ERR_INVALID_ARG;
        if(cJSON_IsString(key))memset(key->valuestring,0,strlen(key->valuestring));
        memset(body,0,sizeof(body));cJSON_Delete(j);
        if(result!=ESP_OK)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Credential could not be saved. Check the value and retry.");
        station_transport_refresh();return httpd_resp_sendstr(r,"{\"saved\":true}");
    }
    /* Authorised: only reachable after the authorised() gate at the top of post(). */
    if(!strcmp(r->uri,"/api/update")) {
        const cJSON *action=cJSON_GetObjectItemCaseSensitive(j,"action");bool accepted=false;
        if(cJSON_IsString(action)&&!strcmp(action->valuestring,"configure")) {
            const cJSON *hours=cJSON_GetObjectItemCaseSensitive(j,"hours"),*automatic=cJSON_GetObjectItemCaseSensitive(j,"automatic"),*start=cJSON_GetObjectItemCaseSensitive(j,"start"),*end=cJSON_GetObjectItemCaseSensitive(j,"end");
            if(cJSON_IsNumber(hours)&&cJSON_IsBool(automatic)&&cJSON_IsNumber(start)&&cJSON_IsNumber(end)&&
               (hours->valuedouble==1||hours->valuedouble==720)&&start->valuedouble>=0&&start->valuedouble<1440&&end->valuedouble>=0&&end->valuedouble<1440&&floor(start->valuedouble)==start->valuedouble&&floor(end->valuedouble)==end->valuedouble)
                accepted=station_update_configure((unsigned)hours->valueint,cJSON_IsTrue(automatic),(unsigned)start->valueint,(unsigned)end->valueint);
        }else if(cJSON_IsString(action))accepted=station_update_request(action->valuestring);
        cJSON_Delete(j);if(!accepted)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid update request or update service busy.");
        httpd_resp_set_status(r,"202 Accepted");return httpd_resp_sendstr(r,"{\"accepted\":true}");
    }
    bool accepted=false,valid=false;
    if(!strcmp(r->uri,"/api/location")) {
        char coordinates[64],name[64];valid=station_phone_parse_home(j,coordinates,name);
        station_network_status_t net;station_network_status(&net);
        if(location_busy&&net.location_completion!=location_completion)location_busy=false;
        if(valid&&!location_busy) {
            location_completion=net.location_completion;
            accepted=station_network_home(coordinates,name);location_busy=accepted;
        }
    } else {
        station_phone_command_t cmd={0};
        portENTER_CRITICAL(&mux);cmd.settings=current_settings;portEXIT_CRITICAL(&mux);
        if(!strcmp(r->uri,"/api/settings")) {cmd.change_settings=true;valid=station_phone_parse_settings(j,&cmd.settings);}
        else {const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,"view");valid=cJSON_IsNumber(v)&&isfinite(v->valuedouble)&&v->valuedouble>=0&&v->valuedouble<VIEW_COUNT&&floor(v->valuedouble)==v->valuedouble;if(valid){cmd.change_view=true;cmd.view=v->valueint;}}
        if(valid)accepted=xQueueSend(queue,&cmd,0)==pdTRUE;
    }
    cJSON_Delete(j);
    if(!valid)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid settings or location");
    if(!accepted){httpd_resp_set_status(r,"503 Service Unavailable");return httpd_resp_sendstr(r,"Busy. Please retry.");}
    httpd_resp_set_status(r,"202 Accepted");httpd_resp_set_type(r,"application/json");return httpd_resp_sendstr(r,"{\"accepted\":true}");
}
static esp_err_t serial(httpd_req_t *r,esp_err_t (*handler)(httpd_req_t *))
{
    xSemaphoreTake(handler_lock,portMAX_DELAY);esp_err_t err=handler(r);xSemaphoreGive(handler_lock);return err;
}
static esp_err_t status_route(httpd_req_t *r){return serial(r,status_get);}
static esp_err_t post_route(httpd_req_t *r){return serial(r,post);}
/* Ports 443 and 80 serve exactly the same page and API. */
static esp_err_t register_routes(httpd_handle_t handle)
{
    const httpd_uri_t routes[]={
        {.uri="/api/credential",.method=HTTP_POST,.handler=post_route},{.uri="/api/update",.method=HTTP_POST,.handler=post_route},
        {.uri="/connection.js",.method=HTTP_GET,.handler=asset},
        {.uri="/",.method=HTTP_GET,.handler=asset},{.uri="/app.js",.method=HTTP_GET,.handler=asset},
        {.uri="/style.css",.method=HTTP_GET,.handler=asset},{.uri="/location.js",.method=HTTP_GET,.handler=asset},
        {.uri="/api/status",.method=HTTP_GET,.handler=status_route},{.uri="/api/settings",.method=HTTP_POST,.handler=post_route},
        {.uri="/api/location",.method=HTTP_POST,.handler=post_route},{.uri="/api/view",.method=HTTP_POST,.handler=post_route}};
    esp_err_t err=ESP_OK;
    for(unsigned i=0;i<sizeof(routes)/sizeof(routes[0])&&err==ESP_OK;i++)err=httpd_register_uri_handler(handle,&routes[i]);
    return err;
}
static httpd_config_t server_config(void)
{
    httpd_config_t config=HTTPD_DEFAULT_CONFIG();config.stack_size=10240;config.max_open_sockets=3;
    config.lru_purge_enable=true;config.recv_wait_timeout=3;config.send_wait_timeout=3;config.max_uri_handlers=11;
    return config;
}
static esp_err_t http_close(void)
{
    if(!http_server)return ESP_OK;
    portENTER_CRITICAL(&mux);http_ready=false;portEXIT_CRITICAL(&mux);
    esp_err_t err=httpd_stop(http_server);
    if(err==ESP_OK){http_server=NULL;ESP_LOGI("phone","Local mode: port 80 closed");}
    return err;
}
static void http_open(void)
{
    if(http_server&&(http_ready||http_close()!=ESP_OK))return;
    httpd_config_t config=server_config();
    /* Fewer sockets than HTTPS; HTTPS keeps the default control port. */
    config.server_port=80;config.ctrl_port=32769;config.max_open_sockets=2;
    if(httpd_start(&http_server,&config)!=ESP_OK){http_server=NULL;return;}
    if(register_routes(http_server)!=ESP_OK){http_close();return;}
    portENTER_CRITICAL(&mux);http_ready=true;portEXIT_CRITICAL(&mux);
    ESP_LOGW("phone","Local mode: unencrypted phone page open on port 80");
}
unsigned station_phone_local_mode(int request)
{
    if(request==STATION_LOCAL_REQUEST_ON||request==STATION_LOCAL_REQUEST_OFF)atomic_store(&local_request,request);
    station_network_status_t net;station_network_status(&net);
    esp_netif_t *iface=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");esp_netif_ip_info_t info;
    bool ip_ready=iface&&esp_netif_get_ip_info(iface,&info)==ESP_OK&&info.ip.addr;
    portENTER_CRITICAL(&mux);
    station_local_mode_t mode=local_mode;bool failed=local_failed,ready=server_ready;esp_err_t tls=tls_error;
    bool dns_ready=ip_ready&&dns_address==info.ip.addr&&esp_timer_get_time()<dns_verified_until;
    portEXIT_CRITICAL(&mux);
    unsigned flags=mode==STATION_LOCAL_MODE_ON?STATION_LOCAL_ON:0;
    if(atomic_load(&local_request)!=STATION_LOCAL_QUERY||mode==STATION_LOCAL_MODE_ROTATE)flags|=STATION_LOCAL_BUSY;
    if(failed)flags|=STATION_LOCAL_FAILED;
    if(!ready&&tls!=ESP_OK)flags|=STATION_LOCAL_OFFER_NO_HTTPS;
    flags|=certificate_offers();
    if(ready&&net.connected&&dns_ready)flags|=STATION_LOCAL_HTTPS_READY;
    return flags;
}
esp_err_t station_phone_start(const station_settings_t *settings)
{
    if(!queue){
        station_phone_publish(settings,VIEW_CLOCK);
        esp_err_t pairing=station_pairing_load(token);if(pairing!=ESP_OK)return pairing;
        /* An unreadable flag fails closed: port 80 stays shut and the token is replaced. */
        station_local_mode_t saved;if(station_local_mode_load(&saved)!=ESP_OK)saved=STATION_LOCAL_MODE_ROTATE;
        if(!handler_lock&&!(handler_lock=xSemaphoreCreateMutex()))return ESP_ERR_NO_MEM;
        queue=xQueueCreate(2,sizeof(station_phone_command_t));if(!queue)return ESP_ERR_NO_MEM;
        portENTER_CRITICAL(&mux);local_mode=saved;portEXIT_CRITICAL(&mux);
    }
    if(local_mode==STATION_LOCAL_MODE_ON)http_open(); /* Works without any device identity. */
    if(server_ready)return ESP_OK;
    if(server)return ESP_FAIL; /* Failed shutdown still owns its TLS state. */
    httpd_config_t config=server_config();
    esp_err_t err=station_phone_tls_start(&server,&config);
    portENTER_CRITICAL(&mux);tls_error=err;portEXIT_CRITICAL(&mux);
    if(err!=ESP_OK){server=NULL;return err;}
    if((err=register_routes(server))!=ESP_OK){if(station_phone_tls_stop(server)==ESP_OK)server=NULL;else tls_reload_pending=true;portENTER_CRITICAL(&mux);tls_error=err;portEXIT_CRITICAL(&mux);return err;}
    tls_reload_pending=false;
    portENTER_CRITICAL(&mux);server_ready=true;tls_error=ESP_OK;portEXIT_CRITICAL(&mux);
    ESP_LOGI("phone","Local phone control ready");return ESP_OK;
}
