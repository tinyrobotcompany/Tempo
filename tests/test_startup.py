"""Guard the adapter's task-notification contract at application startup.

The DOUBLE_FULL rotated flush waits on the current task's notification, but
VSYNC wakes the adapter task. main must never render synchronously, nor gate
backlight restoration on frame completion. Host LVGL's immediate flush stub
cannot reproduce this FreeRTOS/ISR mismatch.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]

class StartupContract(unittest.TestCase):
    def test_main_defers_rendering_and_restores_backlight(self):
        source = (ROOT / 'firmware/main/main.c').read_text()
        source = re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)
        startup = source.split('void app_main(void)', 1)[1]
        self.assertNotRegex(startup, r'\b(?:lv_refr_now|lv_timer_handler)\s*\(')
        create = startup.index('station_ui_start(')
        unlock = startup.index('bsp_display_unlock()', create)
        backlight = startup.index('bsp_display_brightness_set(settings.brightness)', unlock)
        self.assertLess(create, unlock)
        self.assertLess(unlock, backlight)
        # Backlight restoration must be unconditional, with no completion wait.
        gap = startup[unlock:backlight]
        self.assertNotRegex(gap, r'\b(?:if|while|for|ulTaskNotifyTake|xSemaphoreTake|vTaskDelay)\s*\(')
        self.assertIn('UI stopped responding', startup[backlight:])

if __name__ == '__main__':
    unittest.main()
