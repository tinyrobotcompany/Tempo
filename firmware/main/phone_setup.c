#include "phone_setup.h"
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
#include "sdkconfig.h"
#include <string.h>
#include <stdio.h>
#include <math.h> /* floor/isfinite validate numeric settings before conversion. */
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static station_settings_t current_settings;
static station_view_t current_view;
static QueueHandle_t queue;
static httpd_handle_t server;
static bool server_ready;
static char token[33];
/* HTTP handlers execute serially. Location handoff is device-owned, not tab-owned. */
static char location_request[33];
static int64_t location_request_until;
static bool location_busy;
static unsigned location_completion;
extern const char connection_js_start[] __asm__("_binary_connection_js_start");
extern const char connection_js_end[] __asm__("_binary_connection_js_end");
extern const char shortcut_js_start[] __asm__("_binary_shortcut_js_start");
extern const char shortcut_js_end[] __asm__("_binary_shortcut_js_end");
#ifdef STATION_HAS_LOCATION_SHORTCUT
extern const char shortcut_start[] __asm__("_binary_Weather_Station_Location_shortcut_start");
extern const char shortcut_end[] __asm__("_binary_Weather_Station_Location_shortcut_end");
#endif
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
    memset(link,0,sizeof(*link));
    station_network_status_t net;station_network_status(&net);
    esp_netif_t *iface=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");esp_netif_ip_info_t info;
    portENTER_CRITICAL(&mux);bool ready=server_ready;portEXIT_CRITICAL(&mux);
    bool ip_ready=iface&&esp_netif_get_ip_info(iface,&info)==ESP_OK&&info.ip.addr;
    int reason=!net.connected?1:!ready?2:!ip_ready?3:0;
    static int previous=-1;
    if(reason!=previous){ESP_LOGI("phone","Setup availability: %s",reason==1?"waiting for Wi-Fi":reason==2?"waiting for HTTP server":reason==3?"waiting for IP address":"ready");previous=reason;}
    if(ready&&net.connected&&ip_ready) {
        link->online=true;
        snprintf(link->url,sizeof(link->url),"http://" IPSTR "/#token=%s",IP2STR(&info.ip),token);
    }
}
static void headers(httpd_req_t *r)
{
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
    char bearer[48],expected[48];snprintf(expected,sizeof(expected),"Bearer %s",token);
    if(httpd_req_get_hdr_value_str(r,"Authorization",bearer,sizeof(bearer))!=ESP_OK)return false;
    if(strlen(bearer)!=strlen(expected))return false;
    unsigned diff=0;for(size_t i=0;i<strlen(expected);i++)diff|=(unsigned char)bearer[i]^(unsigned char)expected[i];
    return !diff;
}
static esp_err_t asset(httpd_req_t *r)
{
    headers(r);const char *start=page_start,*end=page_end,*type="text/html; charset=utf-8";
    if(!strcmp(r->uri,"/connection.js")){start=connection_js_start;end=connection_js_end;type="text/javascript";}
    if(!strcmp(r->uri,"/shortcut.js")){start=shortcut_js_start;end=shortcut_js_end;type="text/javascript";}
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
    cJSON_AddNumberToObject(settings,"nightMode",s.night_mode);cJSON_AddNumberToObject(settings,"nightBrightness",s.night_brightness);
    cJSON_AddNumberToObject(settings,"nightStart",s.night_start);cJSON_AddNumberToObject(settings,"nightEnd",s.night_end);
    cJSON_AddNumberToObject(j,"view",v);cJSON_AddBoolToObject(j,"connected",net.connected);
    cJSON_AddNumberToObject(j,"saveState",station_settings_save_state());
#ifdef STATION_HAS_LOCATION_SHORTCUT
    cJSON_AddBoolToObject(j,"shortcutAvailable",true);
#else
    cJSON_AddBoolToObject(j,"shortcutAvailable",false);
#endif
    cJSON_AddBoolToObject(loc,"valid",net.location_valid);cJSON_AddNumberToObject(loc,"latitude",net.latitude);cJSON_AddNumberToObject(loc,"longitude",net.longitude);
    cJSON_AddStringToObject(loc,"name",net.location_name);cJSON_AddStringToObject(loc,"timezone",net.timezone);
    cJSON_AddStringToObject(loc,"message",net.location_message);cJSON_AddNumberToObject(loc,"completion",net.location_completion);
    cJSON_AddBoolToObject(loc,"success",net.location_success);cJSON_AddBoolToObject(loc,"pending",location_busy);
    station_update_status_t u;station_update_status(&u);cJSON *update=cJSON_AddObjectToObject(j,"update");
    cJSON_AddStringToObject(update,"installed",u.installed);cJSON_AddStringToObject(update,"available",u.available);
    cJSON_AddStringToObject(update,"message",u.message);cJSON_AddStringToObject(update,"notes",u.notes_url);
    cJSON_AddNumberToObject(update,"countdown",u.countdown_seconds);cJSON_AddNumberToObject(update,"state",u.state);cJSON_AddNumberToObject(update,"progress",u.progress);
    cJSON_AddNumberToObject(update,"hours",u.interval_hours);cJSON_AddBoolToObject(update,"automatic",u.automatic);
    cJSON_AddNumberToObject(update,"start",u.window_start);cJSON_AddNumberToObject(update,"end",u.window_end);
    cJSON_AddNumberToObject(update,"lastCheck",(double)u.last_check);cJSON_AddNumberToObject(update,"nextCheck",(double)u.next_check);
    cJSON_AddBoolToObject(j,"transportConfigured",u.credential_configured);
    return json_reply(r,j);
}
static esp_err_t post(httpd_req_t *r)
{
    headers(r);if(!authorised(r))return httpd_resp_send_err(r,HTTPD_401_UNAUTHORIZED,"Scan the QR code again.");
    char type[64];if(httpd_req_get_hdr_value_str(r,"Content-Type",type,sizeof(type))!=ESP_OK||strcmp(type,"application/json"))return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"JSON required");
    if(r->content_len==0||r->content_len>1024)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid request size");
    char body[1025];size_t used=0;
    while(used<r->content_len){int n=httpd_req_recv(r,body+used,r->content_len-used);if(n<=0)return ESP_FAIL;used+=n;}
    if(memchr(body,0,used))return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid JSON");
    body[used]=0;
    cJSON *j=station_phone_json_shallow(body)?cJSON_ParseWithOpts(body,NULL,true):NULL;
    if(!strcmp(r->uri,"/api/location-request")) {
        const cJSON *action=cJSON_GetObjectItemCaseSensitive(j,"action");
        if(!cJSON_IsString(action)) {cJSON_Delete(j);return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid location request");}
        if(!strcmp(action->valuestring,"begin")) {
            uint8_t random[16];esp_fill_random(random,sizeof(random));
            for(unsigned i=0;i<16;i++)snprintf(location_request+2*i,3,"%02x",random[i]);
            location_request_until=esp_timer_get_time()+600000000LL;
        } else if(!strcmp(action->valuestring,"validate")) {
            const cJSON *state=cJSON_GetObjectItemCaseSensitive(j,"state");
            if(!cJSON_IsString(state)||strlen(state->valuestring)!=32||!location_request_until||esp_timer_get_time()>=location_request_until||strcmp(state->valuestring,location_request)) {
                cJSON_Delete(j);httpd_resp_set_status(r,"409 Conflict");return httpd_resp_sendstr(r,"Location request expired or replaced. Start a new location request.");
            }
            /* Idempotent validation permits retry after a lost HTTP reply.
               It never saves coordinates; confirmation uses /api/location. */
        } else {cJSON_Delete(j);return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid location request action");}
        cJSON_Delete(j);cJSON *result=cJSON_CreateObject();
        if(!result)return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"Out of memory");
        cJSON_AddStringToObject(result,"state",location_request);return json_reply(r,result);
    }
    if(!strcmp(r->uri,"/api/credential")) {
        cJSON *key=cJSON_GetObjectItemCaseSensitive(j,"key");
        bool ok=cJSON_IsString(key)&&strlen(key->valuestring)<=STATION_PRIM_KEY_MAX;
        esp_err_t result=ok?station_credential_set(key->valuestring):ESP_ERR_INVALID_ARG;
        if(cJSON_IsString(key))memset(key->valuestring,0,strlen(key->valuestring));
        memset(body,0,sizeof(body));cJSON_Delete(j);
        if(result!=ESP_OK)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Credential could not be saved. Check the value and retry.");
        station_transport_refresh();return httpd_resp_sendstr(r,"{\"saved\":true}");
    }
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
static esp_err_t shortcut_download(httpd_req_t *r)
{
    headers(r);
#ifdef STATION_HAS_LOCATION_SHORTCUT
    /* Generic signed installer: contains no pairing token or customer data. */
    httpd_resp_set_type(r,"application/octet-stream");
    httpd_resp_set_hdr(r,"Content-Disposition","attachment; filename=\"Weather Station Location.shortcut\"");
    return httpd_resp_send(r,shortcut_start,shortcut_end-shortcut_start);
#else
    httpd_resp_set_status(r,"503 Service Unavailable");
    return httpd_resp_sendstr(r,"Location shortcut not included in this build. Use address search.");
#endif
}
esp_err_t station_phone_start(const station_settings_t *settings)
{
    if(server_ready)return ESP_OK;
    if(!queue){
        station_phone_publish(settings,VIEW_CLOCK);uint8_t random[16];esp_fill_random(random,sizeof(random));
        for(unsigned i=0;i<16;i++)snprintf(token+2*i,3,"%02x",random[i]);
        queue=xQueueCreate(2,sizeof(station_phone_command_t));if(!queue)return ESP_ERR_NO_MEM;
    }
    httpd_config_t config=HTTPD_DEFAULT_CONFIG();config.stack_size=8192;config.max_open_sockets=3;
    config.lru_purge_enable=true;config.recv_wait_timeout=3;config.send_wait_timeout=3;config.max_uri_handlers=14;
    esp_err_t err=httpd_start(&server,&config);if(err!=ESP_OK){server=NULL;return err;}
    const httpd_uri_t routes[]={
        {.uri="/api/credential",.method=HTTP_POST,.handler=post},{.uri="/api/update",.method=HTTP_POST,.handler=post},
        {.uri="/api/location-request",.method=HTTP_POST,.handler=post},
        {.uri="/connection.js",.method=HTTP_GET,.handler=asset},
        {.uri="/shortcut.js",.method=HTTP_GET,.handler=asset},{.uri="/Weather-Station-Location.shortcut",.method=HTTP_GET,.handler=shortcut_download},
        {.uri="/",.method=HTTP_GET,.handler=asset},{.uri="/app.js",.method=HTTP_GET,.handler=asset},
        {.uri="/style.css",.method=HTTP_GET,.handler=asset},{.uri="/location.js",.method=HTTP_GET,.handler=asset},
        {.uri="/api/status",.method=HTTP_GET,.handler=status_get},{.uri="/api/settings",.method=HTTP_POST,.handler=post},
        {.uri="/api/location",.method=HTTP_POST,.handler=post},{.uri="/api/view",.method=HTTP_POST,.handler=post}};
    for(unsigned i=0;i<sizeof(routes)/sizeof(routes[0]);i++)if((err=httpd_register_uri_handler(server,&routes[i]))!=ESP_OK){httpd_stop(server);server=NULL;return err;}
    portENTER_CRITICAL(&mux);server_ready=true;portEXIT_CRITICAL(&mux);
    ESP_LOGI("phone","Local phone control ready");return ESP_OK;
}
