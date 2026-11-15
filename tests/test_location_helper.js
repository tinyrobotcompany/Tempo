const test=require('node:test'),assert=require('node:assert/strict'),vm=require('node:vm'),fs=require('node:fs'),path=require('node:path');
const file=path.resolve('companion/location-helper/helper.js');
test('secure helper session validation, permission failures and return URL',async()=>{
 for(const valid of [false,true]){
  const nodes=new Map(),el=id=>{if(!nodes.has(id))nodes.set(id,{});return nodes.get(id);};let point=null,redirect,fail;
  const context={URL,URLSearchParams,document:{getElementById:el},location:{hash:'#token='+'a'.repeat(32),pathname:'/',assign:url=>redirect=url},history:{replaceState(){}},
   StationLocation:{privateReturn:()=>valid?'http://192.168.1.2/':null,validPoint:p=>!!p,shortName:s=>s,
    editor:({onChange})=>{onChange();return{get:()=>point,locate:async()=>{if(fail)throw fail;}};}}};
  vm.runInNewContext(fs.readFileSync(file,'utf8'),context,{filename:file});
  el('saveLocation').onclick();assert.equal(redirect,undefined);
  for(const code of [1,3,0]){fail={code,message:'Network unavailable'};await el('locate').onclick();assert(el('locationMessage').textContent);}
  fail=null;await el('locate').onclick();assert.equal(el('locate').disabled,!valid);
  point={latitude:48,longitude:2,name:'Home'};el('saveLocation').onclick();
  if(valid){assert(redirect.startsWith('http://192.168.1.2/#'));assert(redirect.includes('latitude=48.000000'));}
  else assert.equal(redirect,undefined);
 }
});
