"""Local mode can be switched only from the display, never by a phone, HTTP, cloud or OTA path."""
import re, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def read(p):return (ROOT/p).read_text()
def firmware_sources():
 return [p for p in (ROOT/'firmware').rglob('*') if p.suffix in ('.c','.h','.inc') and 'managed_components' not in p.parts and 'build' not in p.parts]
class LocalModeContract(unittest.TestCase):
 def users(self,symbol):
  return sorted(str(p.relative_to(ROOT)) for p in firmware_sources() if symbol in p.read_text(errors='ignore'))
 def test_only_phone_server_writes_the_flag(self):
  self.assertEqual(self.users('station_local_mode_save'),['firmware/main/phone_setup.c','firmware/main/settings.c','firmware/main/settings.h'])
 def test_only_the_display_can_request_a_change(self):
  allowed={'firmware/main/phone_setup.c','firmware/main/phone_setup.h','firmware/main/main.c'}
  self.assertTrue(set(self.users('station_phone_local_mode'))<=allowed)
  ui=read('firmware/components/station_ui/station_ui.c')
  # Requests are sent only from the confirmation button handler.
  self.assertEqual(len(re.findall(r'actions\.phone_local_mode\((?!STATION_LOCAL_QUERY)',ui)),1)
  self.assertIn('static void confirm_local(lv_event_t *e)\n{\n    int request=(int)(uintptr_t)lv_event_get_user_data(e);\n    if(actions.phone_local_mode)actions.phone_local_mode(request);',ui)
  self.assertEqual(ui.count('STATION_LOCAL_REQUEST_ON'),1);self.assertEqual(ui.count('STATION_LOCAL_REQUEST_OFF'),1)
  self.assertIn('button(content,on?"Confirm: turn off":"Confirm: turn on Local mode"',ui)
 def test_no_phone_route_or_payload_reaches_it(self):
  server=read('firmware/main/phone_setup.c')
  self.assertEqual(server.count('atomic_store(&local_request'),1)
  routes=server[server.index('static esp_err_t register_routes'):server.index('static httpd_config_t server_config')]
  self.assertNotIn('local',routes.lower().replace('local mode',''))
  for name in ('post','status_get','asset'):
   body=server[server.index('static esp_err_t '+name+'(httpd_req_t *r)'):]
   body=body[:body.index('\n}\n')]
   self.assertNotIn('local_request',body);self.assertNotIn('station_local_mode_save',body)
  for p in ['firmware/main/phone_protocol.c','firmware/main/update_service.c','firmware/main/device_enrollment.c','firmware/main/transport_service.c']:
   self.assertNotIn('local_mode',read(p).lower(),p)
  web=''.join(read('firmware/main/web/'+n) for n in ('app.js','connection.js','location.js'))
  self.assertNotRegex(web,r"api\('local|/api/local|localMode\s*:")
 def test_flag_is_outside_the_settings_blob(self):
  settings=read('firmware/main/settings.c')
  self.assertIn('nvs_set_u8(handle, "local_mode"',settings)
  self.assertNotIn('local_mode',read('firmware/components/station_core/station_core.c').lower())
  self.assertNotIn('local_mode',read('firmware/components/station_core/include/station_core.h').lower())
if __name__=='__main__':
 unittest.main(verbosity=0)
 print('PASS')
