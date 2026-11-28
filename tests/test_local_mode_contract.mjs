// Local mode can be switched only from the display, never by a phone, HTTP, cloud or OTA path.
import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,readdirSync,statSync} from 'node:fs';
import {join,relative,extname,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
const root=join(dirname(fileURLToPath(import.meta.url)),'..');
const read=p=>readFileSync(join(root,p),'utf8');
function sources(dir,out=[]){
 for(const name of readdirSync(dir)){
  if(name==='managed_components'||name==='build')continue;
  const path=join(dir,name);
  if(statSync(path).isDirectory())sources(path,out);
  else if(['.c','.h','.inc'].includes(extname(name)))out.push(path);
 }
 return out;
}
const firmware=sources(join(root,'firmware'));
const users=symbol=>firmware.filter(p=>readFileSync(p,'utf8').includes(symbol)).map(p=>relative(root,p)).sort();
test('only the phone server writes the flag',()=>{
 assert.deepEqual(users('station_local_mode_save'),['firmware/main/phone_setup.c','firmware/main/settings.c','firmware/main/settings.h']);
});
test('only the display can request a change',()=>{
 const allowed=new Set(['firmware/main/phone_setup.c','firmware/main/phone_setup.h','firmware/main/main.c']);
 for(const p of users('station_phone_local_mode'))assert(allowed.has(p),p);
 const ui=read('firmware/components/station_ui/station_ui.c');
 // Requests are sent only from the confirmation button handler.
 assert.equal((ui.match(/actions\.phone_local_mode\((?!STATION_LOCAL_QUERY)/g)||[]).length,1);
 assert(ui.includes('static void confirm_local(lv_event_t *e)\n{\n    int request=(int)(uintptr_t)lv_event_get_user_data(e);\n    if(actions.phone_local_mode)actions.phone_local_mode(request);'));
 assert.equal(ui.split('STATION_LOCAL_REQUEST_ON').length-1,1);
 assert.equal(ui.split('STATION_LOCAL_REQUEST_OFF').length-1,1);
 assert(ui.includes('button(content,on?"Confirm: turn off":"Confirm: turn on Local mode"'));
});
test('no phone route or payload reaches it',()=>{
 const server=read('firmware/main/phone_setup.c');
 assert.equal(server.split('atomic_store(&local_request').length-1,1);
 const routes=server.slice(server.indexOf('static esp_err_t register_routes'),server.indexOf('static httpd_config_t server_config'));
 assert(routes.length>0);
 assert(!routes.toLowerCase().replaceAll('local mode','').includes('local'));
 for(const name of ['post','status_get','asset']){
  const start=server.indexOf('static esp_err_t '+name+'(httpd_req_t *r)');
  assert(start>=0,name);
  const body=server.slice(start,server.indexOf('\n}\n',start));
  assert(!body.includes('local_request'),name);assert(!body.includes('station_local_mode_save'),name);
 }
 for(const p of ['firmware/main/phone_protocol.c','firmware/main/update_service.c','firmware/main/device_enrollment.c','firmware/main/transport_service.c'])
  assert(!read(p).toLowerCase().includes('local_mode'),p);
 const web=['app.js','connection.js','location.js'].map(n=>read('firmware/main/web/'+n)).join('');
 assert.doesNotMatch(web,/api\('local|\/api\/local|localMode\s*:/);
});
test('the flag is outside the settings blob',()=>{
 assert(read('firmware/main/settings.c').includes('nvs_set_u8(handle, "local_mode"'));
 assert(!read('firmware/components/station_core/station_core.c').toLowerCase().includes('local_mode'));
 assert(!read('firmware/components/station_core/include/station_core.h').toLowerCase().includes('local_mode'));
});
