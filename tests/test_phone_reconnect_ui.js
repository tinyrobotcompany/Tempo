const assert=require('node:assert/strict'),vm=require('node:vm'),fs=require('node:fs');
const nodes=new Map(),listeners={};
function element(id){if(!nodes.has(id))nodes.set(id,{value:'',textContent:'',disabled:false,hidden:false,classList:{toggle(){}},setAttribute(){},addEventListener(type,fn){this[type]=fn;}});return nodes.get(id);}
let options,connected=false,candidate=null;
const buttons=[element('clockButton'),element('weatherButton')];
const context={URLSearchParams,TextEncoder,Date,console,setTimeout,clearTimeout,
 location:{hash:'#token='+'a'.repeat(32),search:'',pathname:'/'},history:{replaceState(){}},
 sessionStorage:{getItem:()=>null,setItem(){},removeItem(){}},
 document:{hidden:false,getElementById:element,querySelectorAll:()=>buttons,addEventListener:(type,fn)=>listeners[type]=fn},
 window:{addEventListener:(type,fn)=>listeners[type]=fn},
 StationShortcut:{consumeVerified:()=>null},
 StationLocation:{editor:()=>({get:()=>candidate,choose:p=>{candidate=p;}})},
 StationConnection:{create:config=>{options=config;return{connected:()=>connected,start:()=>config.onState({phase:'connecting',failures:0}),wake:()=>{},request:()=>Promise.resolve()};}}
};
vm.runInNewContext(fs.readFileSync('firmware/main/web/app.js','utf8'),context,{filename:require('node:path').resolve('firmware/main/web/app.js')});
assert(element('saveSettings').disabled);assert(element('locate').disabled);
const settings={name:'Simon',brightness:75,theme:1,font:0,dwell:20,color:0x123456,rotate:false};
const status={settings,location:{valid:true,latitude:48,longitude:2,name:'Home',timezone:'Europe/Paris'},shortcutAvailable:true};
connected=true;options.onState({phase:'connected',failures:0});options.onStatus(status);
assert.equal(element('name').value,'Simon');assert(!element('saveSettings').disabled);
element('name').value='Unsaved name';element('settingsForm').input();candidate={latitude:49,longitude:3,name:'Unsaved map'};
options.onStatus({...status,settings:{...settings,brightness:25}});
assert.equal(element('brightness').value,25);assert.equal(element('brightnessValue').textContent,'25%');
assert.equal(element('name').value,'Unsaved name');
connected=false;options.onState({phase:'reconnecting',failures:1});
assert(element('saveSettings').disabled);assert(element('saveLocation').disabled);assert(buttons.every(b=>b.disabled));assert(!element('name').disabled);
assert.equal(element('name').value,'Unsaved name');assert.equal(candidate.name,'Unsaved map');
options.onState({phase:'reconnecting',failures:3});assert(!element('connectionHelp').hidden);
connected=true;options.onState({phase:'connected',failures:0});options.onStatus(status);
assert(!element('saveSettings').disabled);assert(!element('saveLocation').disabled);assert(buttons.every(b=>!b.disabled));
assert.equal(element('name').value,'Unsaved name');assert.equal(candidate.name,'Unsaved map');assert(element('connectionHelp').hidden);
assert(listeners.online&&listeners.pageshow&&listeners.visibilitychange);
console.log('PASS: actual phone page retains edits/map during reconnect and restores controls without reload');
// Saving feedback must be adjacent to the tapped button before any network reply.
setImmediate(async()=>{
 let saveOptions,rejectStatus;
 const saveContext={...context,StationConnection:{create:config=>{saveOptions=config;return{connected:()=>true,start:()=>{},wake:()=>{},request:()=>new Promise((resolve,reject)=>{rejectStatus=reject;})};}}};
 vm.runInNewContext(fs.readFileSync('firmware/main/web/app.js','utf8'),saveContext,{filename:require('node:path').resolve('firmware/main/web/app.js')});
 saveOptions.onState({phase:'connected',failures:0});saveOptions.onStatus(status);
 const saving=element('saveLocation').onclick();
 assert.equal(element('saveLocation').textContent,'Saving location…');assert(element('saveLocation').disabled);
 assert.equal(element('locationSaveStatus').textContent,'Saving location…');
 rejectStatus(new Error('Test failure'));await saving;
 assert.equal(element('locationSaveStatus').textContent,'Test failure');assert.equal(element('saveLocation').textContent,'Confirm location');
 console.log('PASS: immediate button-level saving feedback and adjacent failure message');
});

setImmediate(async()=>{
 let viewOptions,finish;buttons[0].dataset={view:'0'};buttons[0].textContent='Clock';
 const viewContext={...context,StationConnection:{create:config=>{viewOptions=config;return{connected:()=>true,start:()=>{},wake:()=>{},request:()=>new Promise(resolve=>{finish=resolve;})};}}};
 vm.runInNewContext(fs.readFileSync('firmware/main/web/app.js','utf8'),viewContext,{filename:require('node:path').resolve('firmware/main/web/app.js')});
 viewOptions.onState({phase:'connected',failures:0});viewOptions.onStatus(status);
 const request=buttons[0].onclick();assert.equal(buttons[0].textContent,'Opening…');assert(buttons.every(b=>b.disabled));
 assert.equal(element('message').textContent,'Changing the display view…');finish();await request;
 assert.equal(buttons[0].textContent,'Clock');assert(!buttons[0].disabled);
 console.log('PASS: view button gives immediate progress and restores after reply');
});

// Exercise the actual idle form, including presets, custom values and persisted confirmation.
setImmediate(async()=>{
 const localNodes=new Map();let idleOptions,submitted,requests=0;
 function field(id){if(!localNodes.has(id))localNodes.set(id,{value:'',textContent:'',disabled:false,hidden:false,focus(){this.focused=true;},classList:{toggle(){}},setAttribute(){},addEventListener(type,fn){this[type]=fn;}});return localNodes.get(id);}
 const idleContext={...context,document:{...context.document,getElementById:field,querySelectorAll:()=>[]},
  StationConnection:{create:config=>{idleOptions=config;return{connected:()=>true,start:()=>{},wake:()=>{},request:async(path,body)=>{requests++;if(path==='settings'){submitted=body;return{};}return{settings:submitted,saveState:2};}};}}};
 vm.runInNewContext(fs.readFileSync('firmware/main/web/app.js','utf8'),idleContext,{filename:require('node:path').resolve('firmware/main/web/app.js')});
 idleOptions.onState({phase:'connected',failures:0});
 idleOptions.onStatus({...status,settings:{...settings,dimAfter:3,offAfter:5,dimBrightness:10}});
 assert.equal(field('dimPreset').value,'custom');assert.equal(field('offPreset').value,'5');
 field('dimPreset').value='2';field('dimPreset').onchange();assert.equal(field('dimAfter').value,'2');
 field('dimPreset').value='custom';field('dimPreset').onchange();assert(field('dimAfter').focused);
 field('offAfter').value='1';field('offAfter').oninput();assert.equal(field('offPreset').value,'1');
 await field('settingsForm').onsubmit({preventDefault(){}});assert.equal(requests,0);assert.match(field('settingsMessage').textContent,/later than dim/);
 field('offAfter').value='4';
 field('settingsForm').input({target:{id:'offAfter'}});
 idleOptions.onStatus({...status,settings:{...settings,brightness:25,dimAfter:3,offAfter:5,dimBrightness:10}});
 assert.equal(field('dimAfter').value,'2');assert.equal(field('offAfter').value,'4');
 assert.equal(field('brightness').value,25);
 const pending=field('settingsForm').onsubmit({preventDefault(){}});
 assert.equal(field('settingsMessage').textContent,'Saving…');assert(field('saveSettings').disabled);
 field('name').value='Edited during save';field('settingsForm').input({target:{id:'name'}});
 await pending;assert.equal(submitted.dimAfter,2);assert.equal(submitted.offAfter,4);assert.equal(submitted.dimBrightness,10);assert.equal(submitted.brightness,25);
 assert.match(field('settingsMessage').textContent,/SAVED/);assert(!field('saveSettings').disabled);
 idleOptions.onStatus({...status,settings:{...submitted,dimAfter:5,offAfter:10,brightness:15}});
 assert.equal(field('dimAfter').value,5);assert.equal(field('offAfter').value,10);assert.equal(field('brightness').value,15);
 assert.equal(field('name').value,'Edited during save');
 console.log('PASS: idle presets/custom minutes, conflicting timer rejection and persisted save feedback');
});

setImmediate(async()=>{
 let secureOptions,located=0,deny=false;
 const secure={...context,isSecureContext:true,navigator:{geolocation:{}},
  StationLocation:{editor:()=>({get:()=>null,choose(){},async locate(){located++;if(deny)throw Error('Location permission denied');}})},
  StationConnection:{create:config=>{secureOptions=config;return{connected:()=>true,start(){},wake(){},request(){throw Error('HTTPS geolocation must not hand credentials to a shortcut');}};}}};
 vm.runInNewContext(fs.readFileSync('firmware/main/web/app.js','utf8'),secure,{filename:require('node:path').resolve('firmware/main/web/app.js')});
 secureOptions.onState({phase:'connected',failures:0});secureOptions.onStatus({...status,shortcutAvailable:false});
 await element('locate').onclick();assert.equal(located,1);
 deny=true;await element('locate').onclick();assert.equal(element('locationMessage').textContent,'Location permission denied');assert(!element('locate').disabled);
 console.log('PASS: HTTPS uses native browser geolocation without transmitting credentials to a callback');
});

// Legacy callback data must not trigger parsing, a request or a saved location.
setImmediate(async()=>{
 let callbacks,requests=0,chosen=0;
 const legacy={...context,isSecureContext:false,navigator:{},
  location:{...context.location,search:'?location_state=old&location_status=success&result=WSLOC1%0A48%0A2'},
  StationLocation:{editor:()=>({get:()=>null,choose(){chosen++;}})},
  StationConnection:{create:config=>{callbacks=config;return{connected:()=>true,start(){},wake(){},request(){requests++;throw Error('Unexpected callback request');}};}}};
 vm.runInNewContext(fs.readFileSync('firmware/main/web/app.js','utf8'),legacy);
 callbacks.onState({phase:'connected',failures:0});callbacks.onStatus({...status,location:{valid:false}});
 await element('locate').onclick();
 assert.equal(requests,0);assert.equal(chosen,0);
 assert.match(element('locationMessage').textContent,/Search for your address/);
 assert(!element('locate').disabled);
 console.log('PASS: obsolete callbacks ignored; unavailable native location keeps address fallback');
});
