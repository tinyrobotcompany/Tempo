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
    def test_trial_rolls_back_on_service_failure_or_deadline(self):
        source=(ROOT/'firmware/main/main.c').read_text()
        block=re.search(r'if \(pending && \(update_err.*?\n        }',source,re.S).group()
        code='''#include <stdbool.h>
#include <assert.h>
#define ESP_OK 0
#define ESP_LOGE(...) ((void)0)
#define ESP_ERROR_CHECK(x) (x)
static int calls;
static long now;
static long esp_timer_get_time(void){return now;}
static int esp_ota_mark_app_invalid_rollback_and_reboot(void){calls++;return 0;}
static int check(bool pending,int update_err,long elapsed){calls=0;now=elapsed;long started=0;
'''+block+'''
return calls;}
int main(void){assert(check(true,-1,5000000)==1);assert(check(true,0,5000000)==0);assert(check(true,0,180000000)==1);assert(check(false,-1,180000000)==0);}
'''
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);(p/'trial.c').write_text(code)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'trial.c'),'-o',str(p/'trial')],check=True)
            subprocess.run([str(p/'trial')],check=True)

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
