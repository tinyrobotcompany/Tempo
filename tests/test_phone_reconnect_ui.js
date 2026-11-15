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
connected=false;options.onState({phase:'reconnecting',failures:1});
assert(element('saveSettings').disabled);assert(element('saveLocation').disabled);assert(buttons.every(b=>b.disabled));assert(!element('name').disabled);
assert.equal(element('name').value,'Unsaved name');assert.equal(candidate.name,'Unsaved map');
options.onState({phase:'reconnecting',failures:3});assert(!element('connectionHelp').hidden);
connected=true;options.onState({phase:'connected',failures:0});options.onStatus(status);
assert(!element('saveSettings').disabled);assert(!element('saveLocation').disabled);assert(buttons.every(b=>!b.disabled));
assert.equal(element('name').value,'Unsaved name');assert.equal(candidate.name,'Unsaved map');assert(element('connectionHelp').hidden);
assert(listeners.online&&listeners.pageshow&&listeners.visibilitychange);
console.log('PASS: actual phone page retains edits/map during reconnect and restores controls without reload');
// Shortcuts can return in a brand-new tab with entirely empty browser storage.
(async()=>{
 const calls=[];let callbackOptions,parsed=false;
 const returned={latitude:48.81,longitude:2.22,name:'Phone position'};
 const fresh={...context,location:{...context.location,search:'?location_state='+'b'.repeat(32)+'&location_status=success&result=WSLOC1%0A48.81%0A2.22'},
  StationShortcut:{consumeVerified:()=>{parsed=true;return{point:returned};}},
  StationConnection:{create:config=>{callbackOptions=config;return{connected:()=>true,start:()=>{},wake:()=>{},request:async(path,body)=>{calls.push({path,body});assert.equal(path,'location-request');assert.equal(body.action,'validate');assert.equal(body.state,'b'.repeat(32));return{state:body.state};}};}}
 };
 vm.runInNewContext(fs.readFileSync('firmware/main/web/app.js','utf8'),fresh,{filename:require('node:path').resolve('firmware/main/web/app.js')});
 assert(!parsed);callbackOptions.onState({phase:'connected',failures:0});callbackOptions.onStatus(status);
 for(let i=0;i<10;i++)await Promise.resolve();
 assert(parsed);assert.equal(candidate,returned);assert.equal(calls.length,1);
 callbackOptions.onStatus(status);assert.equal(calls.length,1);
 console.log('PASS: new-tab callback with empty session storage validates on device, displays position, never auto-saves');
})().catch(e=>{console.error(e);process.exitCode=1;});
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
