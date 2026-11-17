const assert=require('node:assert/strict');
require('../firmware/main/web/connection.js');
const flush=async()=>{for(let i=0;i<20;i++)await Promise.resolve();};
const status={settings:{name:'Saved name'},location:{valid:false}};
function harness(){
 let nextId=0,active=0,maximum=0;const timers=new Map(),calls=[],states=[],results=[];
 const client=StationConnection.create({token:'a'.repeat(32),setTimer:(fn,delay)=>{const id=++nextId;timers.set(id,{fn,delay});return id;},clearTimer:id=>timers.delete(id),onState:s=>states.push(s),onStatus:s=>results.push(s),fetch:(path,options)=>new Promise((resolve,reject)=>{
  active++;maximum=Math.max(maximum,active);let done=false;
  const finish=fn=>value=>{if(done)return;done=true;active--;fn(value);};
  calls.push({path,options,resolve:finish(resolve),reject:finish(reject)});
  options.signal.addEventListener('abort',()=>{const e=new Error('timeout');e.name='AbortError';finish(reject)(e);});
 })});
 const reply=(code=200,data=status)=>calls.at(-1).resolve({ok:code>=200&&code<300,status:code,json:async()=>data});
 const fire=delay=>{const entry=[...timers].find(([,t])=>t.delay===delay);assert(entry,`timer ${delay} missing`);timers.delete(entry[0]);entry[1].fn();};
 return {client,calls,states,results,timers,reply,fire,maximum:()=>maximum};
}
(async()=>{
 const h=harness();h.client.start();await flush();
 h.client.wake();h.client.wake();await flush();assert.equal(h.calls.length,1);
 h.fire(6000);await flush();assert.equal(h.states.at(-1).phase,'reconnecting');
 h.fire(1000);await flush();h.calls.at(-1).reject(new TypeError('Network lost'));await flush();
 h.fire(2000);await flush();h.reply();await flush();assert(h.client.connected());
 assert.equal(h.maximum(),1);assert.equal(h.results.length,1);
 // A write can be applied despite a lost reply. Never replay it automatically.
 const write=h.client.request('settings',{name:'New name'});await flush();h.calls.at(-1).reject(new TypeError('Lost reply'));
 await assert.rejects(write,/Save was not confirmed/);await flush();
 h.fire(1000);await flush();h.reply();await flush();assert(h.client.connected());
 assert.equal(h.calls.filter(c=>c.options.method==='POST').length,1);
 const read1=h.client.request('status'),read2=h.client.request('status');assert.equal(read1,read2);await flush();h.reply();await read1;
 // Active update status polls every second and new versions arrive without refreshing.
 h.fire(3000);await flush();h.reply(200,{...status,update:{installed:'0.9.0',available:'0.9.1',state:2,countdown:45}});await flush();
 h.fire(1000);await flush();h.calls.at(-1).reject(new TypeError('Device restarting'));await flush();
 h.fire(1000);await flush();h.reply(200,{...status,update:{installed:'0.9.1',available:'',state:0}});await flush();
 assert.equal(h.results.at(-1).update.installed,'0.9.1');assert(h.client.connected());
 // A revoked pairing token is an authentication failure, not endless reconnection.
 h.client.wake();await flush();h.reply(401);await flush();assert.equal(h.states.at(-1).phase,'pairing');assert.equal(h.timers.size,0);
 const count=h.calls.length;h.client.wake();await flush();assert.equal(h.calls.length,count);
 h.client.stop();
 const capped=harness();capped.client.start();await flush();
 for(const delay of [1000,2000,4000,8000,15000,15000]){capped.calls.at(-1).reject(new TypeError('offline'));await flush();capped.fire(delay);await flush();}
 capped.reply();await flush();assert(capped.client.connected());assert.equal(capped.maximum(),1);capped.client.stop();
 console.log('PASS: automatic timeout recovery, retry backoff, request deduplication, no write replay, pairing expiry');
})().catch(e=>{console.error(e);process.exitCode=1;});
