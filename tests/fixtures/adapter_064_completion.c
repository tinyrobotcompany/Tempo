/* Upstream esp_lvgl_adapter 0.6.4 / 5d75f3f0dc499d9ed4b69284a3741187c2b75a70
 * Unmodified function bodies used to reproduce the lost-completion race.
 * Copyright Espressif Systems. SPDX-License-Identifier: Apache-2.0 */

static inline void display_bridge_v9_mark_frame_buf_submit(esp_lv_adapter_display_bridge_v9_t *impl)
{
    if (!bridge_mode_needs_submit_gated_frame_complete(impl)) {
        return;
    }

    portENTER_CRITICAL(&impl->double_sync_lock);
    impl->frame_buf_submit_pending = true;
    portEXIT_CRITICAL(&impl->double_sync_lock);
}

static bool IRAM_ATTR display_bridge_v9_consume_frame_buf_submit_isr(esp_lv_adapter_display_bridge_v9_t *impl)
{
    if (!bridge_mode_needs_submit_gated_frame_complete(impl)) {
        return true;
    }

    bool submit_pending = false;
    portENTER_CRITICAL_ISR(&impl->double_sync_lock);
    submit_pending = impl->frame_buf_submit_pending;
    if (submit_pending) {
        impl->frame_buf_submit_pending = false;
    }
    portEXIT_CRITICAL_ISR(&impl->double_sync_lock);

    return submit_pending;
}

static bool display_bridge_v9_handle_frame_buf_complete(esp_lv_adapter_display_bridge_v9_t *impl)
{
    if (!impl) {
        return false;
    }

    BaseType_t need_yield = pdFALSE;

    display_bridge_v9_signal_dummy_draw_event(impl, ESP_LV_ADAPTER_DUMMY_DRAW_EVT_FRAME_DONE, &need_yield);

    const esp_lv_adapter_tear_avoid_mode_t mode = impl->cfg.base.tear_avoid_mode;
    if (!bridge_mode_uses_buf_switch_release(mode)) {
        return (need_yield == pdTRUE);
    }

    bool switched = true;
    if (mode == ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_FULL &&
            !bridge_mode_uses_rotated_nonpartial_pipeline_release(impl)) {
        switched = false;
        portENTER_CRITICAL_ISR(&impl->pipeline.lock);
        if (impl->pending_fb) {
            impl->disp_fb = impl->pending_fb;
            impl->pending_fb = NULL;
            switched = true;
        }
        portEXIT_CRITICAL_ISR(&impl->pipeline.lock);
    }

    if (!switched) {
        return false;
    }

    if (!display_bridge_v9_consume_frame_buf_submit_isr(impl)) {
        return false;
    }

    impl->switch_seq++;

    if (bridge_mode_uses_rotated_nonpartial_pipeline_release(impl) ||
            mode == ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_FULL ||
            mode == ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL ||
            mode == ESP_LV_ADAPTER_TEAR_AVOID_MODE_DOUBLE_PARTIAL) {
        display_bridge_pipeline_release_buf_isr(&impl->pipeline);

        TaskHandle_t notify_task = display_bridge_v9_get_notify_task(impl);
        if (notify_task) {
            vTaskNotifyGiveFromISR(notify_task, &need_yield);
        }
    } else {
        bool double_ready = display_bridge_v9_complete_double_wait_isr(impl);
        if (double_ready && impl->double_wait_sem) {
            xSemaphoreGiveFromISR(impl->double_wait_sem, &need_yield);
        }
    }

    return (need_yield == pdTRUE);
}
