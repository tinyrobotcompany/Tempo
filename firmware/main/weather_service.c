#include "weather_service.h"
#include "network_time.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define BODY_LIMIT 24576
static const char *TAG="weather";
static portMUX_TYPE weather_mux=portMUX_INITIALIZER_UNLOCKED;
static station_weather_snapshot_t snapshot={.state=WEATHER_LOCATION};
static bool refresh_requested;
static int64_t last_attempt_us=-60000000;
static int64_t fetched_us;
typedef struct {char body[BODY_LIMIT];size_t used;bool overflow;} weather_response_t;
static esp_err_t receive_body(esp_http_client_event_t *event)
{
    weather_response_t *r=event->user_data;
    if(event->event_id==HTTP_EVENT_ON_DATA && event->data_len>0) {
        if((size_t)event->data_len>=sizeof(r->body)-r->used){r->overflow=true;return ESP_FAIL;}
        memcpy(r->body+r->used,event->data,event->data_len);r->used+=event->data_len;r->body[r->used]=0;
    }
    return ESP_OK;
}
static esp_err_t fetch(const station_network_status_t *home,station_weather_data_t *data)
{
    char url[1024];if(!station_weather_url(url,sizeof(url),home->latitude,home->longitude))return ESP_ERR_INVALID_ARG;
    weather_response_t *response=calloc(1,sizeof(*response));if(!response)return ESP_ERR_NO_MEM;
    /* ESP-IDF requires the complete GET request line to fit in its TX buffer.
     * Its 512-byte default is too small for the current/hourly/daily query.
     * Cover the maximum URL plus method/protocol/CRLF and terminator. */
    esp_http_client_config_t config={.url=url,.timeout_ms=12000,.buffer_size_tx=sizeof(url)+32,
        .crt_bundle_attach=esp_crt_bundle_attach,
        .event_handler=receive_body,.user_data=response,.disable_auto_redirect=true};
    esp_http_client_handle_t client=esp_http_client_init(&config);
    if(!client){free(response);return ESP_ERR_NO_MEM;}
    esp_err_t err=esp_http_client_perform(client);int http=esp_http_client_get_status_code(client);
    if(err==ESP_OK && (http!=200||response->overflow||!station_weather_parse(response->body,time(NULL),data)))err=ESP_FAIL;
    /* Do not log request URLs, coordinates, credentials or response bodies. */
    ESP_LOGI(TAG,"Fetch complete: HTTP %d, %u bytes, %s",http,(unsigned)response->used,esp_err_to_name(err));
    esp_http_client_cleanup(client);free(response);return err;
}
void station_weather_status(station_weather_snapshot_t *result)
{
    portENTER_CRITICAL(&weather_mux);*result=snapshot;int64_t fetched=fetched_us;portEXIT_CRITICAL(&weather_mux);
    result->stale=station_weather_stale(&result->data,time(NULL)) ||
        (result->data.valid && esp_timer_get_time()-fetched>=1800000000LL);
}
bool station_weather_refresh(void)
{
    int64_t now=esp_timer_get_time();
    portENTER_CRITICAL(&weather_mux);
    bool accept=snapshot.state!=WEATHER_LOADING && now-last_attempt_us>=60000000;
    if(accept)refresh_requested=true;
    portEXIT_CRITICAL(&weather_mux);return accept;
}
static void set_state(station_weather_state_t state)
{
    portENTER_CRITICAL(&weather_mux);
    if(snapshot.state!=state){snapshot.state=state;snapshot.revision++;}
    portEXIT_CRITICAL(&weather_mux);
}
static void weather_worker(void *arg)
{
    (void)arg;unsigned generation=UINT32_MAX;int64_t next_attempt=0;unsigned retry_seconds=60;bool was_connected=false;
    for(;;) {
        station_network_status_t home;station_network_status(&home);
        if(home.connected && !was_connected) {
            int64_t earliest=last_attempt_us+60000000;
            if(next_attempt>earliest)next_attempt=earliest;
            portENTER_CRITICAL(&weather_mux);bool cached=snapshot.data.valid;portEXIT_CRITICAL(&weather_mux);
            if(cached)set_state(WEATHER_READY);
        }
        was_connected=home.connected;
        if(home.location_generation!=generation) {
            generation=home.location_generation;next_attempt=0;retry_seconds=60;
            portENTER_CRITICAL(&weather_mux);
            unsigned revision=snapshot.revision+1;
            memset(&snapshot,0,sizeof(snapshot));snapshot.revision=revision;snapshot.location_generation=generation;
            memcpy(snapshot.location,home.location_name,sizeof(snapshot.location));
            refresh_requested=false;
            portEXIT_CRITICAL(&weather_mux);
        }
        if(!home.location_valid)set_state(WEATHER_LOCATION);
        else if(!home.connected)set_state(WEATHER_WIFI);
        else if(home.source==TIME_UNSET)set_state(WEATHER_TIME);
        else {
            portENTER_CRITICAL(&weather_mux);bool requested=refresh_requested;refresh_requested=false;portEXIT_CRITICAL(&weather_mux);
            int64_t now=esp_timer_get_time();
            if(requested||now>=next_attempt) {
                set_state(WEATHER_LOADING);
                portENTER_CRITICAL(&weather_mux);last_attempt_us=now;portEXIT_CRITICAL(&weather_mux);
                station_weather_data_t data;
                esp_err_t err=fetch(&home,&data);
                station_network_status_t latest;station_network_status(&latest);
                if(latest.location_generation==generation) {
                    portENTER_CRITICAL(&weather_mux);
                    if(err==ESP_OK){snapshot.data=data;fetched_us=esp_timer_get_time();}
                    snapshot.state=err==ESP_OK?WEATHER_READY:WEATHER_RETRY;snapshot.revision++;
                    portEXIT_CRITICAL(&weather_mux);
                    next_attempt=esp_timer_get_time()+(int64_t)(err==ESP_OK?600:retry_seconds)*1000000;
                    retry_seconds=err==ESP_OK?60:(retry_seconds<300?retry_seconds*2:600);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
esp_err_t station_weather_start(void)
{
    return xTaskCreate(weather_worker,"weather",8192,NULL,3,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
