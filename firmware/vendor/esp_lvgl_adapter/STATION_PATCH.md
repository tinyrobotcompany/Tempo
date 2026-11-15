# Local display-adapter correction

Base: Espressif esp_lvgl_adapter 0.6.4, upstream revision 5d75f3f0dc499d9ed4b69284a3741187c2b75a70. Upstream licence retained. Only production-code change: `src/display/bridge/v9/lvgl_bridge_v9.c`; exact diff in `station-frame-completion.patch`. main/idf_component.yml selects this directory explicitly using override_path. Managed dependencies and ESP-IDF are not patched in place.

Rotated DOUBLE_FULL starts with no spare panel buffer. The flush submits draw_fb, publishes frame_buf_submit_pending, enqueues the retired disp_fb, then waits for that buffer to return. The DMA/frame-completion ISR can run between publication and enqueue. Previously it consumed the pending flag and attempted to release from an empty busy_list. Once the task enqueued disp_fb, every subsequent ISR saw no pending submission and skipped the release forever. The application watchdog then restarted the board after the UI heartbeat stopped.

The ISR now leaves the pending submission untouched while the rotated pipeline has no retired buffer. MIPI DMA frame-completion callbacks recur each scanout (confirmed in installed IDF 6.1 esp_lcd_panel_dpi.c). A later callback releases the queued buffer and wakes the adapter task. There is no extra timeout, polling task, watchdog suppression, or nested spinlock. The rendering task is the only producer and the frame-completion ISR is the consumer; the busy-list check is protected by its existing spinlock. Nonrotated and partial-mode paths are unchanged.

`python3 tests/test_display_race.py` extracts/compiles the actual patched ISR and pipeline-list functions. An upstream function fixture reproduces the indefinitely stranded buffer; patched code succeeds with interrupts before publication, between publication and enqueue, after enqueue, and duplicate completion events. This establishes the code race and its correction. The test is not a physical PPA/DSI simulation and does not establish that no other hardware defect exists.

Keep this override until an upstream release incorporates an equivalent correction; review the patch and rerun its deterministic test before changing adapter versions.
