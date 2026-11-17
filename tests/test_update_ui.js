const assert=require('node:assert/strict'),vm=require('node:vm'),fs=require('node:fs');
const nodes=new Map();function el(id){if(!nodes.has(id))nodes.set(id,{value:'',textContent:'',disabled:false,hidden:false,checked:false,classList:{toggle(){}},setAttribute(){},addEventListener(t,f){this[t]=f;}});return nodes.get(id);}
let options,online=true,fail=false,calls=[];
const context={URLSearchParams,TextEncoder,Date,console,setTimeout,clearTimeout,
 location:{hash:'#token='+'a'.repeat(32),search:'',pathname:'/'},history:{replaceState(){}},sessionStorage:{getItem:()=>null,setItem(){},removeItem(){}},
 document:{hidden:false,getElementById:el,querySelectorAll:()=>[],addEventListener(){}},window:{addEventListener(){}},
 StationShortcut:{},StationLocation:{editor:()=>({get:()=>null,choose(){}})},
 StationConnection:{create:o=>{options=o;return{connected:()=>online,start:()=>{},wake(){},request:async(path,body)=>{calls.push({path,body});if(fail)throw Error('Test failure');return{saved:true};}};}}};
vm.runInNewContext(fs.readFileSync('firmware/main/web/app.js','utf8'),context,{filename:require('node:path').resolve('firmware/main/web/app.js')});
const status={settings:{name:'',brightness:65,theme:0,font:0,dwell:20,color:0,rotate:false},location:{valid:false},update:{installed:'0.9.0',available:'0.9.1',message:'Downloading',state:3,progress:50,hours:720,automatic:true,start:180,end:240,notes:'https://github.com/tinyrobotcompany/Tempo/releases/tag/v0.9.1'},transportConfigured:true};
(async()=>{
 options.onState({phase:'connected',failures:0});options.onStatus(status);assert.equal(el('updateProgress').value,50);assert.equal(el('updateHours').value,720);assert.equal(el('updateStart').value,'03:00');assert(!el('releaseNotes').hidden);
 el('updateForm').input();options.onStatus({...status,transportConfigured:false,update:{...status.update,notes:'javascript:alert(1)',available:''}});assert.equal(el('releaseNotes').href,'#');
 options.onStatus({...status,update:{...status.update,state:2,countdown:42}});
 assert.equal(el('updateCountdown').textContent,'Installing in 42 seconds');assert(!el('installUpdate').disabled);
 options.onStatus({...status,update:{...status.update,state:0,available:'',installed:'0.9.1',lastCheck:123,upToDate:true}});
 assert.match(el('updateTitle').textContent,/No new updates/);assert.match(el('updateVersion').textContent,/0.9.1/);assert(el('installUpdate').disabled);
 options.onStatus({...status,update:{...status.update,state:2,countdown:0}});
 await el('checkUpdate').onclick();assert.equal(calls.at(-1).body.action,'check');await el('installUpdate').onclick();assert.equal(calls.at(-1).body.action,'install_now');await el('postponeUpdate').onclick();
 el('updateEnd').value='03:00';el('updateForm').onsubmit({preventDefault(){}});assert(el('updateMessage').textContent.includes('different'));
 el('updateEnd').value='04:00';await el('updateForm').onsubmit({preventDefault(){}});assert.equal(calls.at(-1).body.hours,720);
 fail=true;await el('checkUpdate').onclick();assert(el('updateMessage').textContent.includes('Test failure'));fail=false;
 el('primKey').value='fixture-key';await el('credentialForm').onsubmit({preventDefault(){}});assert.equal(el('primKey').value,'');assert(el('credentialMessage').textContent.includes('Key saved'));
 fail=true;await el('credentialForm').onsubmit({preventDefault(){}});assert(el('credentialMessage').textContent.includes('Test failure'));
 online=false;const count=calls.length;await el('checkUpdate').onclick();await el('credentialForm').onsubmit({preventDefault(){}});assert.equal(calls.length,count);
 console.log('PASS: update status, release link allowlist, scheduling, visible progress, failures and credential clearing');
})().catch(e=>{console.error(e);process.exitCode=1;});

// Reopening the page at the device origin reuses pairing; denied storage does not crash bootstrap.
for(const mode of ['remembered','blocked','invalid']){
 let received,visibleUrl;
 const storage={getItem(){if(mode==='blocked')throw Error('Storage blocked');return mode==='invalid'?'bad':'b'.repeat(32);},setItem(){if(mode==='blocked')throw Error('Storage blocked');}};
 const freshNodes=new Map();const freshElement=id=>{if(!freshNodes.has(id))freshNodes.set(id,{...el(id)});return freshNodes.get(id);};
 const fresh={...context,document:{...context.document,getElementById:freshElement},location:{hash:mode==='blocked'?'#token='+'c'.repeat(32):'',search:'',pathname:'/'},localStorage:storage,sessionStorage:storage,history:{replaceState(a,b,url){visibleUrl=url;}},
 StationConnection:{create:o=>{received=o.token;return{connected:()=>true,start(){},wake(){},request:async()=>({})};}}};
 vm.runInNewContext(fs.readFileSync('firmware/main/web/app.js','utf8'),fresh,{filename:require('node:path').resolve('firmware/main/web/app.js')});
 assert.equal(received,mode==='remembered'?'b'.repeat(32):mode==='blocked'?'c'.repeat(32):'');
 assert.equal(visibleUrl,mode==='blocked'?'/#token='+'c'.repeat(32):'/');
}
