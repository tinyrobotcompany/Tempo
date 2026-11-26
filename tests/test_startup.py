"""Guard the adapter's task-notification contract at application startup.

The DOUBLE_FULL rotated flush waits on the current task's notification, but
VSYNC wakes the adapter task. main must never render synchronously, nor gate
backlight restoration on frame completion. Host LVGL's immediate flush stub
cannot reproduce this FreeRTOS/ISR mismatch.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class StartupContract(unittest.TestCase):
    def test_real_trial_health_policy(self):
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'trial-health'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
                            '-I'+str(ROOT/'firmware/main'),
                            str(ROOT/'tests/test_trial_health.c'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    def test_main_uses_local_trial_policy_without_phone_gate(self):
        source=(ROOT/'firmware/main/main.c').read_text()
        call=re.search(r'station_trial_health\((.*?)\);',source,re.S).group(1)
        self.assertNotIn('phone_err',call)
        self.assertNotIn('enrollment_err',call)
        self.assertIn('update_err==ESP_OK',call)
        self.assertIn('station_update_validate_trial',call)
        self.assertIn('if (trial == STATION_TRIAL_ROLLBACK)',source)
        self.assertIn('esp_ota_mark_app_invalid_rollback_and_reboot()',source)
        ui=(ROOT/'firmware/components/station_ui/station_ui.c').read_text()
        self.assertIn('lv_timer_create(tick,100,NULL)',ui)
        self.assertIn('atomic_fetch_add(&heartbeat,1)',ui.split('static void tick(',1)[1])

    def test_main_defers_rendering_and_restores_backlight(self):
        source = (ROOT / 'firmware/main/main.c').read_text()
        source = re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)
        startup = source.split('void app_main(void)', 1)[1]
        self.assertNotRegex(startup, r'\b(?:lv_refr_now|lv_timer_handler)\s*\(')
        create = startup.index('station_ui_start(')
        unlock = startup.index('bsp_display_unlock()', create)
        self.assertLess(create, unlock)
        # app_main must never override the UI's policy/tracked brightness.
        self.assertNotIn('bsp_display_brightness_set(', startup)
        self.assertNotIn('bsp_display_backlight_on(', startup)
        self.assertIn('UI stopped responding', startup[unlock:])
        ui = (ROOT / 'firmware/components/station_ui/station_ui.c').read_text()
        init = ui.split('void station_ui_start(', 1)[1]
        self.assertLess(init.index('show_view(VIEW_CLOCK)'), init.index('update_power()'))
        self.assertLess(init.index('update_power()'), init.index('lv_timer_create('))

if __name__ == '__main__':
    unittest.main()
