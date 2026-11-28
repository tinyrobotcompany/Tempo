// Local mode banner and transport-key warning on the real phone page script.
const assert=require('node:assert/strict'),vm=require('node:vm'),fs=require('node:fs'),path=require('node:path');
const file=path.resolve('firmware/main/web/app.js'),source=fs.readFileSync(file,'utf8');
const html=fs.readFileSync('firmware/main/web/index.html','utf8');
function load(protocol){
 const nodes=new Map();
 const el=id=>{if(!nodes.has(id))nodes.set(id,{value:'',textContent:'',disabled:false,hidden:html.includes('id="'+id+'"')&&new RegExp('id="'+id+'"[^>]*hidden').test(html),checked:false,classList:{toggle(){}},setAttribute(){},addEventListener(t,f){this[t]=f;}});return nodes.get(id);};
 let options;
 const context={URLSearchParams,TextEncoder,Date,console,setTimeout,clearTimeout,
  location:{protocol,hash:'#token='+'a'.repeat(32),search:'',pathname:'/'},history:{replaceState(){}},sessionStorage:{getItem:()=>null,setItem(){},removeItem(){}},
  document:{hidden:false,getElementById:el,querySelectorAll:()=>[],addEventListener(){}},window:{addEventListener(){}},
  StationLocation:{editor:()=>({get:()=>null,choose(){}})},
  StationConnection:{create:o=>{options=o;return{connected:()=>true,start(){},wake(){},request:async()=>({saved:true})};}}};
 vm.runInNewContext(source,context,{filename:file});
 return {el,options};
}
const status={settings:{name:'',brightness:65,theme:0,font:0,dwell:20,color:0,rotate:false},location:{valid:false},update:{installed:'0.11.0',state:0,hours:720,automatic:true,start:180,end:240},transportConfigured:false};
// The page ships with the banner and key warning hidden; only script reveals them.
for(const id of ['localBanner','credentialLocalWarning'])assert.match(html,new RegExp('id="'+id+'"[^>]*hidden'));
assert.match(html,/Local mode – not encrypted/);
{ // Plain HTTP (Local mode): permanent banner before and after any status, extra key warning.
 const {el,options}=load('http:');
 assert.equal(el('localBanner').hidden,false);assert.match(el('localDetail').textContent,/not encrypted/);
 assert.equal(el('credentialLocalWarning').hidden,false);assert.match(el('credentialTransport').textContent,/NOT encrypted/);
 options.onState({phase:'connected',failures:0});options.onStatus({...status,localMode:false});
 assert.equal(el('localBanner').hidden,false,'banner never disappears on plain HTTP');assert.equal(el('credentialLocalWarning').hidden,false);
 options.onStatus({...status,localMode:true});assert.equal(el('localBanner').hidden,false);
}
{ // HTTPS with Local mode off: no banner, no warning, original HTTPS wording kept.
 const {el,options}=load('https:');
 assert.equal(el('localBanner').hidden,true);assert.equal(el('credentialLocalWarning').hidden,true);
 assert.equal(el('credentialTransport').textContent,'');
 options.onState({phase:'connected',failures:0});options.onStatus(status);assert.equal(el('localBanner').hidden,true);
 // HTTPS while the display has Local mode on: banner says so, but this page's key form is encrypted.
 options.onStatus({...status,localMode:true});assert.equal(el('localBanner').hidden,false);
 assert.match(el('localDetail').textContent,/This connection is encrypted/);assert.equal(el('credentialLocalWarning').hidden,true);
 options.onStatus({...status,localMode:'yes'});assert.equal(el('localBanner').hidden,true,'only a real boolean turns the banner on');
}
console.log('PASS: Local mode banner on plain HTTP, HTTPS notice, extra transport-key warning');
