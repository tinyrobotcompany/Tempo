'use strict';
const $=id=>document.getElementById(id),L=StationLocation;
const incoming=new URLSearchParams(location.hash.slice(1));
let token=incoming.get('token')||sessionStorage.getItem('stationToken')||'';
if(/^[a-f0-9]{32}$/.test(token))sessionStorage.setItem('stationToken',token);else token='';
// The token stays in the fragment; the callback query carries coordinates to the local device.
// Clear both from the visible URL immediately.
const callbackSearch=location.search;
const callbackState=new URLSearchParams(callbackSearch).get('location_state');
let callbackDone=!callbackState,callbackPending=false;
history.replaceState(null,'',location.pathname);
let state=null,initialised=false,savePending=false,locationPending=false,pendingPoint=null;
let updateEdited=false,updateBusy=false,credentialBusy=false;
function message(text,id='message',error=false){$(id).textContent=text;$(id).classList.toggle('error',error);if(id==='locationMessage'){$('locationSaveStatus').textContent=text;$('locationSaveStatus').classList.toggle('error',error);}}
let appearanceEdited=false;
let linkPhase='connecting',viewPending=false,locatePending=false;
function updateControls(){const ready=linkPhase==='connected';
 for(const id of ['checkUpdate','installUpdate','postponeUpdate','saveUpdates'])$(id).disabled=!ready||updateBusy;
 $('saveCredential').disabled=!ready||credentialBusy;
 $('saveSettings').disabled=!ready||savePending;
 $('saveSettings').setAttribute('aria-busy',String(savePending));
 $('saveSettings').textContent=savePending?'Saving…':'Save settings';
 $('saveLocation').disabled=!ready||locationPending||!editor.get();
 $('saveLocation').textContent=locationPending?'Saving location…':'Confirm location';
 $('saveLocation').classList.toggle('saving',locationPending);
 $('saveLocation').setAttribute('aria-busy',String(locationPending));
 for(const button of document.querySelectorAll('[data-view]'))button.disabled=!ready||viewPending;
 $('locate').disabled=!ready||locatePending;
}
function connectionState({phase,failures}){linkPhase=phase;
 $('connection').textContent=phase==='connected'?'Connected to your display':phase==='pairing'?'Pairing required':phase==='connecting'?'Connecting to your display…':'Reconnecting to your display…';
 $('connection').classList.toggle('reconnecting',phase==='connecting'||phase==='reconnecting');
 $('retryConnection').hidden=phase==='connected'||phase==='pairing';
 $('connectionHelp').hidden=phase==='connected'||(phase!=='pairing'&&failures<3);
 message(phase==='connected'?'':phase==='pairing'?'Scan the display’s current QR code to pair again. Your unsaved changes remain on this page.':'We’ll reconnect automatically. Your changes are kept here.');
 updateControls();
}
const connection=StationConnection.create({token,onState:connectionState,onStatus:applyStatus,hidden:()=>document.hidden});
const api=(path,body)=>connection.request(path,body);
const editor=L.editor({message:text=>message(text,'locationMessage'),onChange:()=>updateControls()});
function minutes(value){const [h,m]=value.split(':').map(Number);return h*60+m;}
function fill(s){for(const key of ['name','brightness','theme','font','dwell'])$(key).value=s[key];$('nightMode').value=s.nightMode??0;$('nightBrightness').value=s.nightBrightness??10;for(const k of ['nightStart','nightEnd']){const n=s[k]??(k==='nightStart'?1380:420);$(k).value=String(Math.floor(n/60)).padStart(2,'0')+':'+String(n%60).padStart(2,'0');}$('rotate').checked=s.rotate;$('color').value='#'+s.color.toString(16).padStart(6,'0');$('brightnessValue').textContent=s.brightness+'%';}
function applyStatus(next){state=next;
 if(next.update){const u=next.update;
  $('updateVersion').textContent=`Installed: ${u.installed} · Available: ${u.available||'none'}`;
  $('updateStatus').textContent=u.message;$('updateStatus').classList.toggle('reconnecting',[1,3,4,5].includes(u.state));$('updateProgress').value=u.progress;
  $('releaseNotes').hidden=!u.notes;$('releaseNotes').href=/^https:\/\/github\.com\/tinyrobotcompany\/Tempo\/releases\/tag\/v[0-9.]+$/.test(u.notes)?u.notes:'#';
  if(!updateEdited){$('updateHours').value=u.hours;$('updateAutomatic').checked=u.automatic;for(const [id,n] of [['updateStart',u.start],['updateEnd',u.end]])$(id).value=String(Math.floor(n/60)).padStart(2,'0')+':'+String(n%60).padStart(2,'0');}
  $('credentialStatus').textContent=next.transportConfigured?'Transport key is saved.':'No transport key configured.';
 }

 const loc=state.location;$('savedLocation').textContent=loc.valid?`${loc.name} · ${loc.timezone}`:'No location saved yet.';
 $('installShortcut').hidden=!state.shortcutAvailable;
 $('shortcutAvailability').textContent=state.shortcutAvailable?'One-time setup. No account, subscription or website required.':'The location shortcut is not included in this firmware yet. You can use address search below.';
 if(!initialised){if(!appearanceEdited)fill(state.settings);initialised=true;
  if(loc.valid)editor.choose(loc);
 }

 updateControls();
 if(!callbackDone&&!callbackPending)receiveLocation();}
async function receiveLocation(){
 callbackPending=true;message('Checking the position returned by your iPhone…','locationMessage');
 try{await api('location-request',{action:'validate',state:callbackState});
  const result=StationShortcut.consumeVerified(callbackSearch);callbackDone=true;
  if(result.point){editor.choose(result.point,true);message('Position received. Check the map and confirm to save.','locationMessage');}
  else message(result.error,'locationMessage',true);
 }catch(e){if(e.status===409||e.status===400)callbackDone=true;message(e.message,'locationMessage',true);}
 finally{callbackPending=false;}
}
$('settingsForm').addEventListener('input',()=>{appearanceEdited=true;});
$('brightness').oninput=()=>{$('brightnessValue').textContent=$('brightness').value+'%';};
$('settingsForm').onsubmit=async e=>{e.preventDefault();if(savePending||!connection.connected())return;const settings={name:$('name').value,brightness:Number($('brightness').value),theme:Number($('theme').value),font:Number($('font').value),color:parseInt($('color').value.slice(1),16),rotate:$('rotate').checked,dwell:Number($('dwell').value),nightMode:Number($('nightMode').value),nightBrightness:Number($('nightBrightness').value),nightStart:minutes($('nightStart').value),nightEnd:minutes($('nightEnd').value)};
 if(settings.nightMode&&settings.nightStart===settings.nightEnd){message('Choose different start and end times.','settingsMessage',true);return;}
 if(new TextEncoder().encode(settings.name).length>48){message('Please use a shorter name.','settingsMessage',true);return;}
 savePending=true;updateControls();message('Saving…','settingsMessage');try{await api('settings',settings);
 // Wait for the UI worker to apply the command and the NVS worker to commit it.
 let saved=false;for(let i=0;i<30;i++){await new Promise(r=>setTimeout(r,300));const s=await api('status');if(Object.keys(settings).every(k=>s.settings[k]===settings[k])&&s.saveState===2){saved=true;break;}if(s.saveState===3)throw new Error('Could not save. Please retry.');}
 if(!saved)throw new Error('Save not confirmed. Check the display before retrying.');message('Settings saved. Night mode applies after 60 seconds idle.','settingsMessage');
 }catch(e){message(connection.connected()?e.message:'Save not confirmed. Your changes are kept here. Check the display after reconnection before saving again.','settingsMessage',true);}finally{savePending=false;updateControls();}};
for(const b of document.querySelectorAll('[data-view]'))b.onclick=async()=>{if(!connection.connected()||viewPending)return;viewPending=true;const caption=b.textContent;b.textContent='Opening…';b.setAttribute('aria-busy','true');message('Changing the display view…');updateControls();try{await api('view',{view:Number(b.dataset.view)});message('Display view requested.');}catch(e){message(e.message,'message',true);}finally{viewPending=false;b.textContent=caption;b.setAttribute('aria-busy','false');updateControls();}};
$('locate').onclick=async()=>{
 if(!connection.connected()||locatePending)return;
 if(!state?.shortcutAvailable){$('shortcutHelp').open=true;message('Automatic positioning is not ready in this firmware. Address search is available below.','locationMessage',true);return;}
 try{locatePending=true;$('locate').setAttribute('aria-busy','true');updateControls();message('Preparing iPhone location…','locationMessage');
 const request=await api('location-request',{action:'begin'});
 message('Opening Shortcuts. If it is missing, use Add location shortcut above.','locationMessage');
 location.assign(StationShortcut.launch(location.origin,token,request.state));
 }catch(e){message(e.message,'locationMessage',true);}finally{locatePending=false;$('locate').setAttribute('aria-busy','false');updateControls();}
};
$('saveLocation').onclick=async()=>{if(locationPending||!connection.connected()||!editor.get())return;pendingPoint={...editor.get()};locationPending=true;updateControls();message('Saving location…','locationMessage');
 try{const before=await api('status');await api('location',pendingPoint);let saved=false;
 for(let i=0;i<40;i++){await new Promise(r=>setTimeout(r,400));const next=await api('status'),loc=next.location;if(loc.completion!==before.location.completion){if(!loc.success)throw new Error('Location could not be saved. Please retry.');if(Math.abs(loc.latitude-pendingPoint.latitude)>0.000002||Math.abs(loc.longitude-pendingPoint.longitude)>0.000002)throw new Error('Location changed on another screen. Check before saving again.');saved=true;break;}}
 if(!saved)throw new Error('Save not confirmed. Check the display before retrying.');message('Location saved. Weather will refresh; local time adjusts automatically.','locationMessage');await api('status');
 }catch(e){message(connection.connected()?e.message:'Location save not confirmed. Your map selection is kept here. Check the saved location after reconnection.','locationMessage',true);}finally{locationPending=false;updateControls();}};
$('retryConnection').onclick=()=>connection.wake();
window.addEventListener('online',()=>connection.wake());
window.addEventListener('pageshow',()=>connection.wake());
document.addEventListener('visibilitychange',()=>{if(!document.hidden)connection.wake();});
connection.start();

async function updateCommand(action,body={}){
 if(updateBusy||!connection.connected())return;
 updateBusy=true;message('Sending update request…','updateMessage');
 for(const id of ['checkUpdate','installUpdate','postponeUpdate','saveUpdates']){$(id).disabled=true;$(id).setAttribute('aria-busy','true');}
 try{await api('update',{action,...body});message('Request accepted. Status will update automatically.','updateMessage');}
 catch(e){message(e.message,'updateMessage',true);}
 finally{updateBusy=false;for(const id of ['checkUpdate','installUpdate','postponeUpdate','saveUpdates']){$(id).setAttribute('aria-busy','false');}updateControls();}
}
$('checkUpdate').onclick=()=>updateCommand('check');$('installUpdate').onclick=()=>updateCommand('install');$('postponeUpdate').onclick=()=>updateCommand('postpone');
$('updateForm').addEventListener('input',()=>{updateEdited=true;});
$('updateForm').onsubmit=e=>{e.preventDefault();const start=minutes($('updateStart').value),end=minutes($('updateEnd').value);if(start===end){message('Choose different start and end times.','updateMessage',true);return;}return updateCommand('configure',{hours:Number($('updateHours').value),automatic:$('updateAutomatic').checked,start,end});};
$('credentialForm').onsubmit=async e=>{e.preventDefault();if(credentialBusy||!connection.connected())return;
 credentialBusy=true;$('saveCredential').disabled=true;$('saveCredential').textContent='Saving…';message('Saving transport key…','credentialMessage');
 try{await api('credential',{key:$('primKey').value});$('primKey').value='';message('Key saved. Transport will test it when refreshing.','credentialMessage');}
 catch(e){message(e.message,'credentialMessage',true);}
 finally{credentialBusy=false;$('saveCredential').textContent='Save transport key';updateControls();}
};
