#include <stdlib.h>
#include <stdatomic.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_lv_adapter_input.h"
#include "station_ui.h"
#include "settings.h"
#include "network_time.h"
#include "weather_service.h"
#include "transport_service.h"
#include "map_tiles.h"
#include "phone_setup.h"

static const char *TAG = "station";
static atomic_uint flushes;
static void frame_submitted(lv_event_t *event)
{
    (void)event; atomic_fetch_add(&flushes,1);
}
static bool save(const station_settings_t *settings) { return station_settings_request_save(settings); }
static bool brightness(unsigned percent) { return bsp_display_brightness_set((int)percent) == ESP_OK; }
/* BSP touch flags stay zero: apply the inverse display rotation exactly once. */
static esp_err_t read_landscape_touch(esp_lcd_touch_handle_t touch,
    esp_lcd_touch_point_data_t *points, uint8_t *count, uint8_t max_count, void *context)
{
    (void)context;
    static bool pressed;
    *count = 0;
    esp_err_t err = esp_lcd_touch_read_data(touch);
    if (err == ESP_OK) err = esp_lcd_touch_get_data(touch, points, count, max_count);
    if (err != ESP_OK) {
        if (pressed) ESP_LOGW(TAG, "Touch read failed: %s", esp_err_to_name(err));
        pressed = false;
        *count = 0;
        return err;
    }
    uint8_t valid = 0;
    for (uint8_t i = 0; i < *count; ++i) {
        uint16_t x, y;
        if (!station_touch_to_landscape(points[i].x, points[i].y, &x, &y)) continue;
        if (!pressed && valid == 0)
            ESP_LOGD(TAG, "Touch raw=(%u,%u) landscape=(%u,%u)",
                     points[i].x, points[i].y, x, y);
        points[valid] = points[i];
        points[valid].x = x;
        points[valid].y = y;
        ++valid;
    }
    if(station_ui_touch(valid>0))valid=0;
    *count = valid;
    pressed = valid > 0;
    return ESP_OK;
}
void app_main(void)
{
    ESP_LOGI(TAG, "Reset reason: %d", (int)esp_reset_reason());
    /* Never silently erase settings on an NVS/schema error. */
    ESP_ERROR_CHECK(nvs_flash_init());
    station_settings_t settings;
    ESP_ERROR_CHECK(station_settings_load(&settings));
    setenv("TZ", "UTC0", 1);
    tzset();
    ESP_ERROR_CHECK(station_settings_worker_start());
    ESP_ERROR_CHECK(station_network_start());
    ESP_ERROR_CHECK(station_weather_start());
    esp_err_t map_err=station_map_tiles_start();
    if(map_err!=ESP_OK)ESP_LOGE(TAG,"Map worker could not start: %s",esp_err_to_name(map_err));
    esp_err_t transport_err=station_transport_start();
    if(transport_err!=ESP_OK)ESP_LOGE(TAG,"Transport task could not start: %s",esp_err_to_name(transport_err));
    esp_err_t phone_err=station_phone_start(&settings);
    if(phone_err!=ESP_OK)ESP_LOGE(TAG,"Phone setup could not start: %s",esp_err_to_name(phone_err));
    bsp_display_cfg_t display = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_90,
        /* Rotated TRIPLE_PARTIAL hits the documented P4 PPA hang on this SDK.
         * Full-frame mode avoids the affected partial-rectangle rotation path. */
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DOUBLE_FULL,
        .touch_flags = {.swap_xy = 0, .mirror_x = 0, .mirror_y = 0},
    };
    ESP_LOGI(TAG, "Display refresh: DOUBLE_FULL, rotation=90 (PPA partial-freeze avoidance)");
    if (!bsp_display_start_with_config(&display) || !bsp_display_get_input_dev()) {
        ESP_LOGE(TAG, "Display/touch initialisation failed; restarting for OTA rollback");
        esp_restart();
    }
    ESP_ERROR_CHECK(bsp_display_lock(UINT32_MAX));
    const esp_lv_adapter_touch_callbacks_t touch_callbacks = {
        .custom_touch_read = read_landscape_touch,
    };
    ESP_ERROR_CHECK(esp_lv_adapter_set_touch_callbacks(bsp_display_get_input_dev(), &touch_callbacks));
    lv_display_add_event_cb(lv_display_get_default(),frame_submitted,LV_EVENT_FLUSH_FINISH,NULL);
    station_ui_start(&settings, (station_ui_actions_t){
        .phone_link=station_phone_link, .phone_take=station_phone_take, .phone_publish=station_phone_publish,
        .transport_search=station_transport_search,.transport_search_status=station_transport_search_status,.transport_search_select=station_transport_search_select,
        .transport_line_map=station_transport_line_map,.transport_line_map_status=station_transport_line_map_status,.transport_line_stop_select=station_transport_line_stop_select,.transport_nearby_reset=station_transport_nearby_reset,
        .map_tile_acquire=station_map_tile_acquire,.map_tile_release=station_map_tile_release,.map_cancel_pending=station_map_cancel_pending,
        .transport_status=station_transport_status,.transport_select=station_transport_select,.transport_refresh=station_transport_refresh,
        .weather_status=station_weather_status, .refresh_weather=station_weather_refresh,
        .save=save, .save_state=station_settings_save_state, .brightness=brightness, .set_time=station_network_manual,
        .connect_wifi=station_network_connect, .automatic_time=station_network_auto,
        .network_status=station_network_status, .set_location=station_network_location,
        .set_city=station_network_city, .scan_wifi=station_network_scan, .wifi_scan_status=station_network_scan_status});
    /* The adapter task owns rendering and receives the VSYNC notifications.
     * Forcing refresh here can wait forever on the wrong task notification. */
    /* Restore under the lock so main cannot overwrite the UI night policy. */
    ESP_ERROR_CHECK(bsp_display_brightness_set(settings.brightness));
    bsp_display_unlock();
    ESP_LOGI(TAG, "UI released to renderer; backlight restored to %u%%", settings.brightness);
    ESP_LOGI(TAG, "UI and network/time service active; OTA download not implemented");
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    bool pending = esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY;
    int64_t started = esp_timer_get_time();
    unsigned previous = 0, stalled = 0, health_ticks=0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        if(phone_err!=ESP_OK){
            phone_err=station_phone_start(&settings);
            if(phone_err!=ESP_OK)ESP_LOGW(TAG,"Phone setup retry failed: %s",esp_err_to_name(phone_err));
        }
        unsigned beat = station_ui_heartbeat();
        stalled = beat == previous ? stalled + 1 : 0;
        previous = beat;
        if(++health_ticks%6==0) {
            ESP_LOGI(TAG,"Display health: heartbeat=%u, submitted_frames=%u, uptime=%lld s",beat,
                     atomic_load(&flushes),(long long)(esp_timer_get_time()/1000000));
            ESP_LOGI(TAG,"Internal memory: free=%u, largest=%u, minimum=%u",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
                     (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
        }
        if (stalled >= 3) {
            ESP_LOGE(TAG, "UI stopped responding; restart");
            esp_restart();
        }
        if (pending && beat >= 100 && esp_timer_get_time()-started >= 120000000) {
            /* Milestone checks only. Expand to networking before enabling OTA delivery. */
            ESP_ERROR_CHECK(esp_ota_mark_app_valid_cancel_rollback());
            pending = false;
            ESP_LOGI(TAG, "Trial firmware healthy");
        }
    }
}
