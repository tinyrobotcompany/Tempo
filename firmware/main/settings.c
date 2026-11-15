#include "settings.h"
#include "nvs.h"
#include <string.h>

/* Fixed byte format avoids struct padding and permits explicit schema migration. */
esp_err_t station_settings_load(station_settings_t *settings)
{
    *settings = station_defaults();
    nvs_handle_t handle;
    esp_err_t err = nvs_open("station", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    uint8_t data[STATION_SETTINGS_BYTES]; size_t size = sizeof(data);
    err = nvs_get_blob(handle, "settings_v3", data, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) { size=sizeof(data); err=nvs_get_blob(handle,"settings_v2",data,&size); }
    if (err == ESP_ERR_NVS_NOT_FOUND) { size=sizeof(data); err=nvs_get_blob(handle,"settings",data,&size); }
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    station_settings_t candidate;
    if (!station_settings_decode(data, size, &candidate)) return ESP_ERR_INVALID_VERSION;
    *settings = candidate;
    return ESP_OK;
}
esp_err_t station_settings_save(const station_settings_t *settings)
{
    if (!station_settings_valid(settings)) return ESP_ERR_INVALID_ARG;
    uint8_t data[STATION_SETTINGS_BYTES];
    station_settings_encode(settings, data);
    nvs_handle_t handle;
    esp_err_t err = nvs_open("station", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, "settings_v3", data, sizeof(data));
    /* Preserve the full v2 appearance record for older firmware rollback. */
    data[0]=2;
    if (err == ESP_OK) err = nvs_set_blob(handle, "settings_v2", data, STATION_SETTINGS_V2_BYTES);
    /* Keep the old record readable by a previous image after OTA rollback. */
    uint8_t legacy[]={1,settings->brightness,settings->theme<3?settings->theme:0,settings->rotate,
                      settings->dwell_seconds&255,settings->dwell_seconds>>8};
    if(err==ESP_OK) err=nvs_set_blob(handle,"settings",legacy,sizeof(legacy));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
typedef struct { station_settings_t settings; unsigned revision; } save_request_t;
static QueueHandle_t save_queue;
static portMUX_TYPE save_mux = portMUX_INITIALIZER_UNLOCKED;
static unsigned save_revision;
static int save_state;
static void settings_worker(void *arg)
{
    (void)arg;
    save_request_t request;
    for (;;) {
        if (xQueueReceive(save_queue, &request, portMAX_DELAY) != pdTRUE) continue;
        /* Coalesce rapid colour/brightness changes without writing every tap. */
        vTaskDelay(pdMS_TO_TICKS(250));
        save_request_t newer;
        while (xQueueReceive(save_queue, &newer, 0) == pdTRUE) request = newer;
        int64_t started = esp_timer_get_time();
        esp_err_t err = station_settings_save(&request.settings);
        ESP_LOGI("settings", "Save completed in %lld ms", (long long)((esp_timer_get_time()-started)/1000));
        portENTER_CRITICAL(&save_mux);
        if (request.revision == save_revision) save_state = err == ESP_OK ? 2 : 3;
        portEXIT_CRITICAL(&save_mux);
        if (err != ESP_OK) ESP_LOGW("settings", "Save failed: %s", esp_err_to_name(err));
    }
}
esp_err_t station_settings_worker_start(void)
{
    save_queue = xQueueCreate(1, sizeof(save_request_t));
    if (!save_queue) return ESP_ERR_NO_MEM;
    return xTaskCreate(settings_worker, "settings_save", 4096, NULL, 2, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
bool station_settings_request_save(const station_settings_t *settings)
{
    if (!save_queue || !station_settings_valid(settings)) return false;
    save_request_t request = {.settings=*settings};
    portENTER_CRITICAL(&save_mux);
    request.revision = ++save_revision; save_state = 1;
    portEXIT_CRITICAL(&save_mux);
    return xQueueOverwrite(save_queue, &request) == pdTRUE;
}
int station_settings_save_state(void)
{
    portENTER_CRITICAL(&save_mux); int result = save_state; portEXIT_CRITICAL(&save_mux);
    return result;
}
