#include "device_enrollment.h"
#include "device_identity.h"
#include "network_time.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "sdkconfig.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

static bool started;
static unsigned failures;
static char last_address[16];
static bool waiting_certificate;
static bool pending_work;
static unsigned retry_seconds;

typedef struct { bool json; unsigned retry; } response_info_t;

static void remember_retry(unsigned seconds)
{
    if(seconds>retry_seconds)retry_seconds=seconds;
}

/* Protocol replies must declare JSON: application/json with optional parameters, any case. */
static esp_err_t response_header(esp_http_client_event_t *event)
{
    if(event->event_id==HTTP_EVENT_ON_HEADER&&event->header_key&&event->header_value&&!strcasecmp(event->header_key,"Content-Type")){
        const char *type=event->header_value;while(*type==' '||*type=='\t')type++;
        size_t length=strlen("application/json");
        ((response_info_t *)event->user_data)->json=!strncasecmp(type,"application/json",length)&&(!type[length]||type[length]==';'||type[length]==' '||type[length]=='\t');
    }
    if(event->event_id==HTTP_EVENT_ON_HEADER&&event->header_key&&event->header_value&&!strcasecmp(event->header_key,"Retry-After")){
        /* Delta-seconds only. Invalid/overflowing headers leave local backoff intact. */
        char *end;errno=0;unsigned long seconds=strtoul(event->header_value,&end,10);
        if(!errno&&end!=event->header_value&&!*end&&seconds<=604800)
            ((response_info_t *)event->user_data)->retry=(unsigned)seconds;
    }
    return ESP_OK;
}
/* Returns the parsed 200/202 reply; *http_status reports which, so callers can require a final 200. */
static cJSON *request(const char *action,const char *value,int *http_status)
{
    *http_status=0;
    cJSON *json=cJSON_CreateObject();if(!json)return NULL;
    unsigned char random[32];char nonce[65];esp_fill_random(random,sizeof(random));
    for(unsigned i=0;i<32;i++)snprintf(nonce+2*i,3,"%02x",random[i]);
    cJSON_AddStringToObject(json,"device_id",station_identity_id());
    cJSON_AddStringToObject(json,"action",action);cJSON_AddStringToObject(json,"nonce",nonce);
    cJSON_AddNumberToObject(json,"issued_at",(double)time(NULL));
    if(value)cJSON_AddStringToObject(json,!strcmp(action,"certificate")?"csr":"address",value);
    char *body=cJSON_PrintUnformatted(json);cJSON_Delete(json);if(!body)return NULL;
    char signature[112];
    if(station_identity_sign(body,signature,sizeof(signature))!=ESP_OK){cJSON_free(body);return NULL;}
    response_info_t response_info={0};
    esp_http_client_config_t config={.url=CONFIG_TEMPO_ENROLLMENT_URL,.timeout_ms=15000,.event_handler=response_header,.user_data=&response_info,
        .crt_bundle_attach=esp_crt_bundle_attach,.disable_auto_redirect=true,.buffer_size=2048,
        .buffer_size_tx=2048};
    esp_http_client_handle_t client=esp_http_client_init(&config);
    if(!client){cJSON_free(body);return NULL;}
    esp_http_client_set_method(client,HTTP_METHOD_POST);
    esp_http_client_set_header(client,"Content-Type","application/json");
    esp_http_client_set_header(client,"X-Tempo-Signature",signature);
    size_t length=strlen(body);esp_err_t err=esp_http_client_open(client,(int)length);
    if(err==ESP_OK){
        size_t sent=0;
        while(sent<length){int n=esp_http_client_write(client,body+sent,(int)(length-sent));if(n<=0){err=ESP_FAIL;break;}sent+=(size_t)n;}
    }
    cJSON_free(body);
    cJSON *reply=NULL;
    if(err==ESP_OK&&esp_http_client_fetch_headers(client)>=0){
        int status=esp_http_client_get_status_code(client);*http_status=status;
        if(status==200||status==202){
            char *response=calloc(1,12289);size_t total=0;bool valid=response!=NULL;
            while(valid&&total<12288){
                int n=esp_http_client_read(client,response+total,12288-total);
                if(n<0){valid=false;break;}if(n==0)break;total+=(size_t)n;
            }
            if(valid&&response_info.json&&total<12288&&esp_http_client_is_complete_data_received(client))reply=cJSON_ParseWithLength(response,total);
            free(response);
        }
    }
    remember_retry(response_info.retry);
    const cJSON *retry=cJSON_GetObjectItemCaseSensitive(reply,"retry_after");
    if(cJSON_IsNumber(retry)&&retry->valuedouble>=0&&retry->valuedouble<=604800)
        remember_retry((unsigned)retry->valuedouble);
    esp_http_client_close(client);esp_http_client_cleanup(client);return reply;
}

static bool current_address(char address[16])
{
    station_network_status_t network;station_network_status(&network);
    if(!network.connected||time(NULL)<1700000000)return false;
    esp_netif_t *iface=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");esp_netif_ip_info_t info;
    if(!iface||esp_netif_get_ip_info(iface,&info)!=ESP_OK||!info.ip.addr)return false;
    snprintf(address,16,IPSTR,IP2STR(&info.ip));
    return true;
}

static bool tick(void)
{
    retry_seconds=0;
    char address[16];if(!current_address(address))return false;
    if(strcmp(last_address,address)){
        int http_status;cJSON *reply=request("address",address,&http_status);if(!reply)return false;
        /* Mark only when status confirms DNS applied; POST acceptance isn't completion. */
        cJSON_Delete(reply);
    }
    /* Status is authoritative only as a final 200; the service never answers status with 202. */
    int http_status;cJSON *status=request("status",NULL,&http_status);if(!status)return false;
    if(http_status!=200){cJSON_Delete(status);return false;}
    const cJSON *dns=cJSON_GetObjectItemCaseSensitive(status,"address");
    if(cJSON_IsString(dns)&&!strcmp(dns->valuestring,address))snprintf(last_address,sizeof(last_address),"%s",address);
    const cJSON *chain=cJSON_GetObjectItemCaseSensitive(status,"certificate");
    const cJSON *state=cJSON_GetObjectItemCaseSensitive(status,"state");
    bool pending=cJSON_IsString(state)&&(!strcmp(state->valuestring,"pending")||!strcmp(state->valuestring,"running"));
    const cJSON *dns_state=cJSON_GetObjectItemCaseSensitive(status,"dns_state");
    bool failed=(cJSON_IsString(state)&&!strcmp(state->valuestring,"error"))||
                (cJSON_IsString(dns_state)&&!strcmp(dns_state->valuestring,"error"));
    pending_work=pending||strcmp(last_address,address)!=0;
    if(failed&&time(NULL)<station_identity_renew_after()){cJSON_Delete(status);return false;}
    if(waiting_certificate&&!pending&&cJSON_IsString(chain)&&chain->valuestring[0]){
        int64_t renewal;
        /* An issued chain that fails local install is retried with worker backoff.
           Return before the renewal block: never request another CSR while one is on offer. */
        if(station_identity_install(chain->valuestring,&renewal)!=ESP_OK){cJSON_Delete(status);return false;}
        waiting_certificate=false;
    }
    cJSON_Delete(status);
    if(time(NULL)>=station_identity_renew_after()&&!pending){
        char *csr=calloc(1,2048);if(!csr)return false;
        esp_err_t err=station_identity_csr(csr,2048);
        cJSON *reply=err==ESP_OK?request("certificate",csr,&http_status):NULL;free(csr);
        if(!reply)return false;
        cJSON_Delete(reply);waiting_certificate=true;pending_work=true;
    }
    return true;
}
typedef struct { int64_t due; char address[16]; } enrollment_schedule_t;

static void scheduled_tick(enrollment_schedule_t *schedule)
{
    char address[16];if(!current_address(address))return; /* Offline is not a provider failure. */
    int64_t now=esp_timer_get_time()/1000000;
    bool changed=strcmp(address,schedule->address)!=0;
    bool renewal_due=!pending_work&&!failures&&time(NULL)>=station_identity_renew_after();
    if(now<schedule->due&&(!changed||failures)&&!renewal_due)return;
    snprintf(schedule->address,sizeof(schedule->address),"%s",address);
    bool ok=tick();failures=ok?0:(failures<3?failures+1:3);
    unsigned seconds=ok?(pending_work?30:86400):(failures==1?300:failures==2?900:3600);
    if(retry_seconds>seconds)seconds=retry_seconds;
    /* Randomize daily reconciliation without moving renewal past its due time. */
    unsigned jitter=ok&&!pending_work?esp_random()%3600:esp_random()%15;
    schedule->due=esp_timer_get_time()/1000000+seconds+jitter;
}

static void worker(void *unused)
{
    (void)unused;
    enrollment_schedule_t schedule={0};
    for(;;){
        scheduled_tick(&schedule);
        /* This checks local state only; healthy devices make no cloud request here. */
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
/* Parse in boundary order: "https://", an authority of hostname/port characters only
   (so no userinfo '@', query '?' or fragment '#' can precede the path), then exactly "/v1/device". */
static bool enrollment_url_valid(const char *url)
{
    if(strncmp(url,"https://",8))return false;
    const char *authority=url+8,*end=authority;
    while((*end>='a'&&*end<='z')||(*end>='A'&&*end<='Z')||(*end>='0'&&*end<='9')||*end=='.'||*end=='-'||*end==':')end++;
    return end>authority&&*authority!=':'&&!strcmp(end,"/v1/device");
}
esp_err_t station_enrollment_start(void)
{
    if(started)return ESP_OK;
    /* Manufacturing identity must exist even before the service is configured. */
    esp_err_t err=station_identity_init();if(err!=ESP_OK)return err;
    const char *url=CONFIG_TEMPO_ENROLLMENT_URL;
    if(!url[0])return ESP_OK; /* Local identity ready; no network worker. */
    if(!enrollment_url_valid(url))return ESP_ERR_INVALID_ARG;
    if(xTaskCreate(worker,"enrollment",12288,NULL,3,NULL)!=pdPASS)return ESP_ERR_NO_MEM;
    started=true;return ESP_OK;
}
