/* Local-device transport: bounded, serial requests and automatic read-only recovery. */
(function(root){
'use strict';
function create({token,fetch:send=root.fetch.bind(root),setTimer=setTimeout,clearTimer=clearTimeout,onState=()=>{},onStatus=()=>{},hidden=()=>false}){
 let phase=token?'connecting':'pairing',failures=0,timer=null,tail=Promise.resolve(),statusRequest=null,polling=null,stopped=false;
 const emit=()=>onState({phase,failures});
 function schedule(delay){clearTimer(timer);timer=null;if(!stopped&&phase!=='pairing'&&!hidden())timer=setTimer(poll,delay);}
 function failed(error){if(error.status===401){phase='pairing';clearTimer(timer);}else{phase='reconnecting';failures++;}emit();}
 async function perform(path,body){
  if(body&&phase!=='connected')throw new Error('Waiting for the display. Your changes are still here; save when it reconnects.');
  const controller=new AbortController(),deadline=setTimer(()=>controller.abort(),6000);
  try{
   const response=await send('/api/'+path,{method:body?'POST':'GET',headers:{Authorization:'Bearer '+token,...(body?{'Content-Type':'application/json'}:{})},body:body?JSON.stringify(body):undefined,cache:'no-store',signal:controller.signal});
   if(!response.ok){const e=new Error(response.status===401?'Pairing expired. Scan the current QR code on the display.':response.status===409?'This location request expired or was replaced. Tap Use my iPhone location to start a new request.':response.status===503?'The display is busy. Please retry.':'The display could not accept this change.');e.status=response.status;throw e;}
   const result=await response.json();
   // Only a validated status response establishes that controls can be used.
   if(path==='status'){
    if(!result.settings||!result.location)throw new Error('Invalid display status');
    phase='connected';failures=0;emit();onStatus(result);
   }
   return result;
  }catch(e){
   if(!e.status||e.status===401||e.status>=500){failed(e);schedule(1000);}
   if(!e.status){const uncertain=new Error(body?'Connection interrupted. Save was not confirmed. Your changes are still here; check before saving again.':'Reconnecting to your display…');uncertain.cause=e;throw uncertain;}
   throw e;
  }finally{clearTimer(deadline);}
 }
 function request(path,body){
  if(path==='status'&&statusRequest)return statusRequest;
  const operation=tail.then(()=>perform(path,body));tail=operation.catch(()=>{});
  if(path==='status'){statusRequest=operation;operation.then(()=>{statusRequest=null;},()=>{statusRequest=null;});}
  return operation;
 }
 function poll(){
  if(stopped||phase==='pairing'||hidden())return Promise.resolve();
  if(polling)return polling;
  clearTimer(timer);timer=null;
  polling=request('status').catch(()=>{}).finally(()=>{
   polling=null;schedule(phase==='connected'?5000:Math.min(15000,1000*2**Math.min(Math.max(failures-1,0),4)));
  });return polling;
 }
 return {request,start(){emit();return poll();},wake(){clearTimer(timer);return poll();},stop(){stopped=true;clearTimer(timer);},connected:()=>phase==='connected'};
}
root.StationConnection={create};
})(globalThis);
