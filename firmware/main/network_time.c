#include "network_time.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

static const char *TAG = "network_time";
typedef enum { CONNECT, AUTO_MODE, MANUAL_TIME, LOCATION, SCAN } command_kind_t;
typedef struct { command_kind_t kind; char ssid[33]; char password[64]; bool automatic; time_t time; char coordinates[64]; char zone[64]; char name[64]; } command_t;
static QueueHandle_t commands, clock_commands;
static station_wifi_scan_t scan_status;
static bool scan_done, scan_succeeded;
static int64_t scan_deadline;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static station_network_status_t status = {.automatic=true, .message="Wi-Fi not configured", .timezone="UTC", .tz_rule="UTC0", .location_message="Set home coordinates to determine the local time zone"};
static bool ip_changed;
static bool wifi_ready, wifi_started, has_credentials, sntp_ready;
static int64_t next_attempt, connection_deadline;
static unsigned retry_seconds = 2;
static char home_coordinates[64], home_name[64];
static bool location_pending;
static int64_t next_location_attempt;
#define WIFI_PROFILES 5
typedef struct {char ssid[33],password[64];} wifi_profile_t;
typedef struct {unsigned version;wifi_profile_t entries[WIFI_PROFILES];} wifi_profiles_t;
static wifi_profiles_t profiles={.version=1};
static wifi_profile_t active_profile, confirmed_profile;
static bool confirmed_pending;
static bool remember_pending;
static int profile_index(const char *ssid){for(unsigned i=0;i<WIFI_PROFILES;i++)if(profiles.entries[i].ssid[0]&&!strcmp(profiles.entries[i].ssid,ssid))return (int)i;return -1;}
static void load_profiles(nvs_handle_t h){
 wifi_profiles_t saved={0};size_t size=sizeof(saved);
 if(nvs_get_blob(h,"wifi_profiles",&saved,&size)!=ESP_OK||size!=sizeof(saved)||saved.version!=1)return;
 for(unsigned i=0;i<WIFI_PROFILES;i++)if(saved.entries[i].ssid[32]||saved.entries[i].password[63]||(saved.entries[i].ssid[0]&&!station_wifi_credentials_valid(saved.entries[i].ssid,saved.entries[i].password)))return;
 profiles=saved;memset(&saved,0,sizeof(saved));
}
static esp_err_t remember_network(const wifi_profile_t *confirmed){
 wifi_profiles_t next={.version=1};next.entries[0]=*confirmed;unsigned n=1;
 for(unsigned i=0;i<WIFI_PROFILES&&n<WIFI_PROFILES;i++)if(profiles.entries[i].ssid[0]&&strcmp(profiles.entries[i].ssid,confirmed->ssid))next.entries[n++]=profiles.entries[i];
 nvs_handle_t h;esp_err_t err=nvs_open("station_net",NVS_READWRITE,&h);
 if(err==ESP_OK){err=nvs_set_blob(h,"wifi_profiles",&next,sizeof(next));if(err==ESP_OK)err=nvs_set_blob(h,"wifi",confirmed,sizeof(*confirmed));if(err==ESP_OK)err=nvs_commit(h);nvs_close(h);}
 if(err==ESP_OK){profiles=next;portENTER_CRITICAL(&mux);for(unsigned i=0;i<scan_status.count;i++)scan_status.networks[i].saved=profile_index(scan_status.networks[i].ssid)>=0;scan_status.revision++;portEXIT_CRITICAL(&mux);}
 memset(&next,0,sizeof(next));return err;
}
static void location_message(const char *text)
{
    portENTER_CRITICAL(&mux);
    snprintf(status.location_message, sizeof(status.location_message), "%s", text);
    portEXIT_CRITICAL(&mux);
}
typedef struct { char body[4096]; size_t used; bool overflow; } response_t;
static esp_err_t http_data(esp_http_client_event_t *event)
{
    response_t *response = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        if (response->used + (size_t)event->data_len >= sizeof(response->body)) {
            response->overflow = true; return ESP_FAIL;
        }
        memcpy(response->body + response->used, event->data, event->data_len);
        response->used += event->data_len;
        response->body[response->used] = 0;
    }
    return ESP_OK;
}
/* A location lookup supplies an IANA name; bundled rules handle DST offline. */
static esp_err_t resolve_timezone(void)
{
    double lat, lon;
    if (!station_parse_coordinates(home_coordinates, &lat, &lon)) return ESP_ERR_INVALID_ARG;
    char url[256];
    snprintf(url, sizeof(url), "https://api.open-meteo.com/v1/forecast?latitude=%.6f&longitude=%.6f&current=temperature_2m&timezone=auto", lat, lon);
    response_t *response = calloc(1, sizeof(*response));
    if (!response) return ESP_ERR_NO_MEM;
    esp_http_client_config_t config = {.url=url, .timeout_ms=10000,
        .crt_bundle_attach=esp_crt_bundle_attach, .event_handler=http_data,
        .user_data=response, .disable_auto_redirect=true};
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { free(response); return ESP_ERR_NO_MEM; }
    esp_err_t err = esp_http_client_perform(client);
    if (esp_http_client_get_status_code(client) != 200 || response->overflow) err = ESP_FAIL;
    esp_http_client_cleanup(client);
    cJSON *json = err == ESP_OK ? cJSON_Parse(response->body) : NULL;
    cJSON *zone = json ? cJSON_GetObjectItemCaseSensitive(json, "timezone") : NULL;
    const char *rule = cJSON_IsString(zone) ? station_timezone_rule(zone->valuestring) : NULL;
    if (!rule) err = ESP_FAIL;
    if (err == ESP_OK && strlen(zone->valuestring) < sizeof(status.timezone)) {
        nvs_handle_t handle;
        err = nvs_open("station_net", NVS_READWRITE, &handle);
        if (err == ESP_OK) {
            err = nvs_set_str(handle, "zone", zone->valuestring);
            if (err == ESP_OK) err = nvs_set_u8(handle,"zone_pending",0);
            if (err == ESP_OK) err = nvs_commit(handle);
            nvs_close(handle);
        }
        if (err == ESP_OK) {
            portENTER_CRITICAL(&mux);
            snprintf(status.timezone, sizeof(status.timezone), "%s", zone->valuestring);
            snprintf(status.tz_rule, sizeof(status.tz_rule), "%s", rule);
            snprintf(status.location_message, sizeof(status.location_message), "Home time zone: %s", zone->valuestring);
            portEXIT_CRITICAL(&mux);
        }
    } else if (err == ESP_OK) err = ESP_FAIL;
    cJSON_Delete(json); free(response);
    return err;
}
/* Publish only committed location changes; consumers never read NVS themselves. */
static void publish_location(const char *coordinates)
{
    double latitude,longitude;
    if(!station_parse_coordinates(coordinates,&latitude,&longitude))return;
    char name[64]="Home location";
    for(unsigned i=0;i<station_city_count;i++) if(!strcmp(coordinates,station_cities[i].coordinates)) {
        snprintf(name,sizeof(name),"%s",station_cities[i].name);break;
    }
    if(home_name[0]) snprintf(name,sizeof(name),"%s",home_name);
    portENTER_CRITICAL(&mux);
    status.location_valid=true;status.latitude=latitude;status.longitude=longitude;
    memcpy(status.location_name,name,sizeof(name));status.location_generation++;
    portEXIT_CRITICAL(&mux);
}
static esp_err_t save_location(const char *coordinates, const char *zone, const char *name)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("station_net", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, "location", coordinates);
    if (err == ESP_OK) err = nvs_set_str(handle, "place", name);
    if (err == ESP_OK && zone[0]) err = nvs_set_str(handle, "zone", zone);
    if (err == ESP_OK) err = nvs_set_u8(handle,"zone_pending",!zone[0]);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) {
        snprintf(home_coordinates, sizeof(home_coordinates), "%s", coordinates);
        snprintf(home_name,sizeof(home_name),"%s",name);
        location_pending = !zone[0]; next_location_attempt = 0;
        portENTER_CRITICAL(&mux);
        /* Keep the last working zone until the coordinate lookup succeeds. */
        if(zone[0]) {
            snprintf(status.timezone,sizeof(status.timezone),"%s",zone);
            snprintf(status.tz_rule,sizeof(status.tz_rule),"%s",station_timezone_rule(zone));
        }
        portEXIT_CRITICAL(&mux);
        publish_location(coordinates);
        location_message(zone[0] ? "Home city saved" : "Location saved; resolving time zone");
    }
    return err;
}

static void message(const char *text)
{
    portENTER_CRITICAL(&mux);
    snprintf(status.message, sizeof(status.message), "%s", text);
    portEXIT_CRITICAL(&mux);
}
void station_network_status(station_network_status_t *result)
{
    portENTER_CRITICAL(&mux); *result = status; portEXIT_CRITICAL(&mux);

}
static void save_confirmed_network(void)
{
    wifi_profile_t confirmed={0};
    portENTER_CRITICAL(&mux);
    bool pending=confirmed_pending;
    if(pending){confirmed=confirmed_profile;confirmed_pending=false;memset(&confirmed_profile,0,sizeof(confirmed_profile));}
    portEXIT_CRITICAL(&mux);
    if(pending){
        if(remember_network(&confirmed)!=ESP_OK)message("Connected, but password could not be saved. Reconnect to retry.");
        else message("Connected. Network remembered.");
        memset(&confirmed,0,sizeof(confirmed));
    }
}
static void sync_callback(struct timeval *time)
{
    portENTER_CRITICAL(&mux);
    if (status.automatic && time->tv_sec >= 1704067200LL) {
        status.source = TIME_NETWORK;
        status.last_sync = time->tv_sec;
    }
    portEXIT_CRITICAL(&mux);
    ESP_LOGI(TAG, "Time synchronised");
}
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        wifi_event_sta_scan_done_t *result = data;
        portENTER_CRITICAL(&mux);
        scan_done = true; scan_succeeded = result && result->status == 0;
        portEXIT_CRITICAL(&mux);
        return;
    }
    if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED){
        const wifi_event_sta_disconnected_t *event=data;
        ESP_LOGW(TAG,"Wi-Fi disconnected: reason=%u",event?(unsigned)event->reason:0);
    } else if(base==IP_EVENT&&id==IP_EVENT_STA_GOT_IP)ESP_LOGI(TAG,"Wi-Fi connected: IP address acquired");
    portENTER_CRITICAL(&mux);
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        status.connected = true; status.connecting = false;
        snprintf(status.message, sizeof(status.message), "Wi-Fi connected");
        if(remember_pending){confirmed_profile=active_profile;confirmed_pending=true;remember_pending=false;}
        ip_changed = true;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        status.connected = false;
        snprintf(status.message, sizeof(status.message), "Wi-Fi disconnected - retrying (check network/password)");
        ip_changed = true;
    }
    portEXIT_CRITICAL(&mux);
}
static esp_err_t configure_sntp(bool automatic)
{
    if (sntp_ready) { esp_netif_sntp_deinit(); sntp_ready = false; }
    if (!automatic) return ESP_OK;
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(3,
        ESP_SNTP_SERVER_LIST("pool.ntp.org", "time.cloudflare.com", "time.google.com"));
    config.sync_cb = sync_callback;
    config.start = false;
    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) return err;
    sntp_ready = true;
    station_network_status_t current; station_network_status(&current);
    return current.connected ? esp_netif_sntp_start() : ESP_OK;
}
static esp_err_t store_mode(bool automatic)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("station_net", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(handle, "auto", automatic);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
static esp_err_t init_wifi(void)
{
    if (wifi_ready) {
        esp_err_t err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
        return err == ESP_OK ? esp_wifi_set_mode(WIFI_MODE_STA) : err;
    }
    /* Hosted C6 setup may block: never call this from the LVGL task. */
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&config);
    if (err != ESP_OK) return err;
    wifi_ready = true;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    return err;
}
static esp_err_t connect_to(const char *ssid, const char *password, bool persist)
{
    int saved=profile_index(ssid);
    if(!password[0]&&saved>=0)password=profiles.entries[saved].password;

    connection_deadline=esp_timer_get_time()+45000000;
    portENTER_CRITICAL(&mux); status.connecting=true; portEXIT_CRITICAL(&mux);
    esp_err_t err = init_wifi();
    if (err != ESP_OK) return err;
    if (wifi_started) {
        esp_wifi_disconnect();
        err = esp_wifi_stop();
        if (err != ESP_OK) return err;
        wifi_started = false;
    }
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, ssid, strlen(ssid));
    memcpy(config.sta.password, password, strlen(password));
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;
    err = esp_wifi_set_config(WIFI_IF_STA, &config);
    memset(&config, 0, sizeof(config));
    if (err != ESP_OK) return err;
    portENTER_CRITICAL(&mux); status.connected = false; portEXIT_CRITICAL(&mux);
    portENTER_CRITICAL(&mux);
    memset(&active_profile,0,sizeof(active_profile));snprintf(active_profile.ssid,sizeof(active_profile.ssid),"%s",ssid);snprintf(active_profile.password,sizeof(active_profile.password),"%s",password);
    remember_pending=persist||saved<0;
    portEXIT_CRITICAL(&mux);
    err = esp_wifi_start();
    if (err != ESP_OK) return err;
    wifi_started = true;
    has_credentials = true;
    retry_seconds = 2;
    message("Connecting to Wi-Fi...");
    next_attempt = esp_timer_get_time() + 15000000;
    return esp_wifi_connect();
}
void station_network_scan_status(station_wifi_scan_t *result)
{
    portENTER_CRITICAL(&mux); *result = scan_status; portEXIT_CRITICAL(&mux);
}
static bool scan_is_running(void)
{
    portENTER_CRITICAL(&mux); bool running = scan_status.state == WIFI_SCAN_RUNNING; portEXIT_CRITICAL(&mux);
    return running;
}
static void scan_failed(const char *reason)
{
    ESP_LOGW(TAG,"Wi-Fi scan: %s",reason);
    char text[96]; snprintf(text, sizeof(text), "%s", reason);
    portENTER_CRITICAL(&mux);
    scan_status.state = WIFI_SCAN_FAILED; scan_status.revision++;
    memcpy(scan_status.message, text, sizeof(text));
    portEXIT_CRITICAL(&mux);
}
static esp_err_t begin_scan(void)
{
    ESP_LOGI(TAG,"Wi-Fi scan: starting");
    esp_err_t err = init_wifi();
    if (err == ESP_OK && !wifi_started) {
        err = esp_wifi_start();
        if (err == ESP_OK) wifi_started = true;
    }
    portENTER_CRITICAL(&mux); scan_done = false; bool connected = status.connected; portEXIT_CRITICAL(&mux);
    /* A station in its initial connect/reconnect attempt cannot scan concurrently. */
    if (err == ESP_OK && has_credentials && !connected) esp_wifi_disconnect();
    wifi_scan_config_t config = {.show_hidden=false, .scan_type=WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active={.min=40, .max=120}};
    if (err == ESP_OK) err = esp_wifi_scan_start(&config, false);
    if (err == ESP_OK) scan_deadline = esp_timer_get_time() + 20000000;
    else scan_failed("Could not scan. Tap Rescan to retry.");
    return err;
}
static void poll_scan(void)
{
    if (!scan_is_running() || scan_deadline == 0) return;
    portENTER_CRITICAL(&mux);
    bool done = scan_done, success = scan_succeeded;
    scan_done = false;
    portEXIT_CRITICAL(&mux);
    if (!done && esp_timer_get_time() < scan_deadline) return;
    scan_deadline = 0;
    if (!done || !success) {
        esp_wifi_scan_stop(); esp_wifi_clear_ap_list();
        scan_failed(done ? "Scan failed. Tap Rescan to retry." : "Scan timed out. Tap Rescan to retry.");
        next_attempt = 0;
        return;
    }
    uint16_t count = 64;
    wifi_ap_record_t *records = calloc(count, sizeof(*records));
    station_wifi_scan_t *result = calloc(1, sizeof(*result));
    if (!records || !result) {
        free(records); free(result); esp_wifi_clear_ap_list();
        scan_failed("Not enough memory for scan results."); return;
    }
    esp_err_t err = esp_wifi_scan_get_ap_records(&count, records);
    if (err == ESP_OK) {
        ESP_LOGI(TAG,"Wi-Fi scan: received %u access points",(unsigned)count);
        result->state = WIFI_SCAN_READY;
        for (unsigned i = 0; i < count; ++i) {
            station_wifi_ap_t ap = {.rssi=records[i].rssi, .secured=records[i].authmode != WIFI_AUTH_OPEN};
            memcpy(ap.ssid, records[i].ssid, 32); ap.ssid[32] = 0;ap.saved=profile_index(ap.ssid)>=0;
            station_scan_add(result, &ap);
        }
        snprintf(result->message, sizeof(result->message), result->count ? "Select a network below" : "No networks found. Tap Rescan.");
        portENTER_CRITICAL(&mux);
        result->revision = scan_status.revision + 1;
        scan_status = *result;
        portEXIT_CRITICAL(&mux);
        scan_deadline = 0;
    } else {
        esp_wifi_clear_ap_list();
        scan_failed("Could not read scan results. Tap Rescan.");
    }
    free(records); free(result);
    next_attempt = 0;
}
static void clock_worker(void *arg)
{
    (void)arg;
    uint8_t automatic = 1;
    /* Automatic time is now always enabled, including migration from manual mode. */
    portENTER_CRITICAL(&mux); status.automatic = automatic != 0; portEXIT_CRITICAL(&mux);
    if (configure_sntp(automatic != 0) != ESP_OK) message("Time service initialisation failed");
    for (;;) {
        command_t cmd;
        if (xQueueReceive(clock_commands, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
            esp_err_t err = ESP_OK;
            int64_t started = esp_timer_get_time();
            {
                bool automatic = cmd.kind == AUTO_MODE && cmd.automatic;
                err = configure_sntp(false);
                if (err == ESP_OK) err = store_mode(automatic);
                if (err == ESP_OK) {
                    portENTER_CRITICAL(&mux); status.automatic = automatic; portEXIT_CRITICAL(&mux);
                    if (cmd.kind == MANUAL_TIME) {
                        struct timeval value = {.tv_sec=cmd.time};
                        if (settimeofday(&value, NULL) != 0) err = ESP_FAIL;
                        else { portENTER_CRITICAL(&mux); status.source = TIME_MANUAL; portEXIT_CRITICAL(&mux); }
                    }
                    if (automatic) err = configure_sntp(true);
                }
                if (err != ESP_OK) {
                    station_network_status_t previous; station_network_status(&previous);
                    configure_sntp(previous.automatic);
                }
                portENTER_CRITICAL(&mux);
                status.time_completion++; status.time_success = err == ESP_OK;
                portEXIT_CRITICAL(&mux);
                message(err == ESP_OK ? (automatic ? "Automatic time enabled" : "Manual time mode") : "Could not change time mode");
            }

            ESP_LOGI(TAG, "Clock request completed: %s, %lld ms", esp_err_to_name(err),
                     (long long)((esp_timer_get_time()-started)/1000));
        }
        station_network_status_t current;
        portENTER_CRITICAL(&mux);
        bool changed = ip_changed; ip_changed = false; current = status;
        portEXIT_CRITICAL(&mux);
        if (changed && current.connected && current.automatic && sntp_ready &&
                esp_netif_sntp_start() != ESP_OK) message("Wi-Fi connected; time sync could not start");
    }
}

static void worker(void *arg)
{
    (void)arg;
    nvs_handle_t handle;
    esp_err_t opened = nvs_open("station_net", NVS_READONLY, &handle);
    char record[97] = {0}; bool stored = false;
    if (opened == ESP_OK) {
        load_profiles(handle);
        size_t size = sizeof(record);
        stored = nvs_get_blob(handle, "wifi", record, &size) == ESP_OK && size == sizeof(record)
            && record[32] == 0 && record[96] == 0 && station_wifi_credentials_valid(record, record+33);
        size_t place_size=sizeof(home_name);
        if(nvs_get_str(handle,"place",home_name,&place_size)!=ESP_OK) home_name[0]=0;
        size_t location_size = sizeof(home_coordinates);
        if (nvs_get_str(handle, "location", home_coordinates, &location_size) == ESP_OK) {
            double lat, lon;
            location_pending = station_parse_coordinates(home_coordinates, &lat, &lon);
            char zone[64]; size_t zone_size = sizeof(zone);
            if (location_pending && nvs_get_str(handle, "zone", zone, &zone_size) == ESP_OK) {
                const char *rule = station_timezone_rule(zone);
                if (rule) {
                    portENTER_CRITICAL(&mux);
                    snprintf(status.timezone, sizeof(status.timezone), "%s", zone);
                    snprintf(status.tz_rule, sizeof(status.tz_rule), "%s", rule);
                    snprintf(status.location_message, sizeof(status.location_message), "Home time zone: %s", zone);
                    portEXIT_CRITICAL(&mux);
                    uint8_t pending=0;
                    nvs_get_u8(handle,"zone_pending",&pending);
                    location_pending = pending != 0;
                }
            }
        }
        nvs_close(handle);
        publish_location(home_coordinates);
    }
    station_network_status_t current; station_network_status(&current);
    if (stored && connect_to(record, record+33, false) != ESP_OK) {
        portENTER_CRITICAL(&mux); status.connecting=false; portEXIT_CRITICAL(&mux);
        message("Wi-Fi initialisation failed - retry in Settings");
    }
    memset(record, 0, sizeof(record));
    for (;;) {
        station_network_status(&current);
        save_confirmed_network();
        command_t cmd;
        if (xQueueReceive(commands, &cmd, pdMS_TO_TICKS(1000)) == pdTRUE) {
            esp_err_t err = ESP_OK;
            if (cmd.kind == CONNECT) {
                connection_deadline = esp_timer_get_time()+45000000;
                err = connect_to(cmd.ssid, cmd.password, true);
                if (err != ESP_OK) { portENTER_CRITICAL(&mux); status.connecting=false; portEXIT_CRITICAL(&mux); }
            }
            else if (cmd.kind == LOCATION) {
                err = save_location(cmd.coordinates, cmd.zone, cmd.name);
                portENTER_CRITICAL(&mux); status.location_completion++; status.location_success=err==ESP_OK; portEXIT_CRITICAL(&mux);
                if(err!=ESP_OK) location_message("Could not save home city. Please retry.");
            }
            else if (cmd.kind == SCAN) err = begin_scan();
            memset(&cmd, 0, sizeof(cmd));
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Network/time operation failed: %s", esp_err_to_name(err));
                message("Operation failed - check connection and retry");
            }
        }
        station_network_status(&current);
        save_confirmed_network();
        int64_t now=esp_timer_get_time();
        portENTER_CRITICAL(&mux);
        status.connection_seconds=status.connecting&&connection_deadline>now?(unsigned)(45-(connection_deadline-now)/1000000):0;
        status.retry_seconds=!status.connected&&!status.connecting&&has_credentials&&next_attempt>now?(unsigned)((next_attempt-now+999999)/1000000):0;
        portEXIT_CRITICAL(&mux);
        poll_scan();
        if (current.connecting && connection_deadline && esp_timer_get_time()>=connection_deadline) {
            portENTER_CRITICAL(&mux); status.connecting=false; portEXIT_CRITICAL(&mux);
            message("Connection timed out. Check the password and try again.");
            connection_deadline=0;
        }
        if (location_pending && current.connected && current.source != TIME_UNSET && esp_timer_get_time() >= next_location_attempt) {
            if (resolve_timezone() == ESP_OK) location_pending = false;
            else location_message("Time zone lookup failed; retrying. Keeping current zone.");
            next_location_attempt = esp_timer_get_time() + 60000000;
        }
        if (wifi_started && has_credentials && !scan_is_running() && !current.connected && esp_timer_get_time() >= next_attempt) {
            esp_wifi_connect();
            next_attempt = esp_timer_get_time() + (int64_t)retry_seconds * 1000000;
            retry_seconds = retry_seconds < 30 ? retry_seconds * 2 : 60;
        }
    }
}
esp_err_t station_network_start(void)
{
    esp_err_t err = esp_netif_init(); if (err != ESP_OK) return err;
    err = esp_event_loop_create_default(); if (err != ESP_OK) return err;
    if (!esp_netif_create_default_wifi_sta()) return ESP_ERR_NO_MEM;
    err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_event, NULL);
    if (err == ESP_OK) err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, wifi_event, NULL);
    if (err == ESP_OK) err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL);
    if (err != ESP_OK) return err;
    commands = xQueueCreate(4, sizeof(command_t));
    clock_commands = xQueueCreate(4, sizeof(command_t));
    if (!commands || !clock_commands) return ESP_ERR_NO_MEM;
    if (xTaskCreate(clock_worker, "station_clock", 6144, NULL, 4, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return xTaskCreate(worker, "network_time", 8192, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
bool station_network_connect(const char *ssid, const char *password)
{
    if (!commands || scan_is_running() || !station_wifi_credentials_valid(ssid, password)) return false;
    command_t cmd = {.kind=CONNECT};
    memcpy(cmd.ssid, ssid, strlen(ssid)); memcpy(cmd.password, password, strlen(password));
    portENTER_CRITICAL(&mux);
    bool busy=status.connecting;
    if (!busy) { status.connecting=true; status.connected=false; }
    portEXIT_CRITICAL(&mux);
    if (busy) { memset(&cmd,0,sizeof(cmd)); return false; }
    bool accepted = xQueueSend(commands, &cmd, 0) == pdTRUE;
    if (!accepted) { portENTER_CRITICAL(&mux); status.connecting=false; portEXIT_CRITICAL(&mux); }
    memset(&cmd, 0, sizeof(cmd));
    return accepted;
}
bool station_network_auto(bool enabled)
{
    command_t cmd = {.kind=AUTO_MODE, .automatic=enabled};
    return clock_commands && xQueueSend(clock_commands, &cmd, 0) == pdTRUE;
}
bool station_network_manual(time_t value)
{
    command_t cmd = {.kind=MANUAL_TIME, .time=value};
    return clock_commands && xQueueSend(clock_commands, &cmd, 0) == pdTRUE;
}

bool station_network_home(const char *coordinates,const char *name)
{
    double lat,lon;
    if(!commands || !coordinates || strlen(coordinates)>=64 || !station_parse_coordinates(coordinates,&lat,&lon) || !name || strlen(name)>=64)return false;
    for(const unsigned char *p=(const unsigned char*)name;*p;p++)if(*p<32||*p==127)return false;
    command_t cmd={.kind=LOCATION};
    snprintf(cmd.coordinates,sizeof(cmd.coordinates),"%.6f,%.6f",lat,lon);
    snprintf(cmd.name,sizeof(cmd.name),"%s",name);
    return xQueueSend(commands,&cmd,0)==pdTRUE;
}
bool station_network_location(const char *coordinates) { return station_network_home(coordinates, "Home location"); }

bool station_network_scan(void)
{
    if (!commands) return false;
    portENTER_CRITICAL(&mux);
    if (scan_status.state == WIFI_SCAN_RUNNING) { portEXIT_CRITICAL(&mux); return false; }
    scan_status.state = WIFI_SCAN_RUNNING; scan_status.revision++;
    portEXIT_CRITICAL(&mux);
    command_t cmd = {.kind=SCAN};
    if (xQueueSend(commands, &cmd, 0) == pdTRUE) return true;
    scan_failed("Network service busy. Tap Rescan to retry.");
    return false;
}

bool station_network_city(unsigned index)
{
    if (!commands || index>=station_city_count || !station_timezone_rule(station_cities[index].zone)) return false;
    command_t cmd={.kind=LOCATION};
    snprintf(cmd.coordinates,sizeof(cmd.coordinates),"%s",station_cities[index].coordinates);
    snprintf(cmd.zone,sizeof(cmd.zone),"%s",station_cities[index].zone);
    snprintf(cmd.name,sizeof(cmd.name),"%s",station_cities[index].name);
    return xQueueSend(commands,&cmd,0)==pdTRUE;
}
