'use strict';
const $=id=>document.getElementById(id),L=StationLocation;
const incoming=new URLSearchParams(location.hash.slice(1));
// Pairing is scoped to this device origin. Storage can be unavailable in private browsers.
function storedToken(){for(const name of ['localStorage','sessionStorage']){try{const value=globalThis[name]?.getItem('stationToken');if(/^[a-f0-9]{32}$/.test(value||''))return value;}catch{}}return '';}
let token=incoming.get('token')||storedToken();
if(!/^[a-f0-9]{32}$/.test(token))token='';
let tokenStored=false;
if(token)for(const name of ['localStorage','sessionStorage']){try{const storage=globalThis[name];if(storage){storage.setItem('stationToken',token);tokenStored=true;}}catch{}}
// If storage is blocked retain only the pairing fragment, so refreshing still works.
history.replaceState(null,'',location.pathname+(!tokenStored&&token?'#token='+token:''));
let state=null,initialised=false,savePending=false,locationPending=false,pendingPoint=null;
let updateEdited=false,updateBusy=false,credentialBusy=false;
function message(text,id='message',error=false){$(id).textContent=text;$(id).classList.toggle('error',error);if(id==='locationMessage'){$('locationSaveStatus').textContent=text;$('locationSaveStatus').classList.toggle('error',error);}}
const appearanceDirty=new Set();
const appearanceKeys=['name','brightness','theme','font','color','rotate','dwell','dimAfter','offAfter','dimBrightness'];
function appearanceValue(key){return key==='rotate'?$('rotate').checked:key==='color'?parseInt($('color').value.slice(1),16):key==='name'?$('name').value:Number($(key).value);}
function markAppearance(event){
 const key=event?.target?.id?.replace(/^(dim|off)Preset$/,'$1After');
 if(appearanceKeys.includes(key))appearanceDirty.add(key);
 else if(state)for(const field of appearanceKeys){if(appearanceValue(field)!==(state.settings[field]??(field==='dimBrightness'?10:0)))appearanceDirty.add(field);}
}
let linkPhase='connecting',viewPending=false,locatePending=false;
function updateControls(){const ready=linkPhase==='connected';
 const u=state?.update,busy=u&&[1,3,4,5,7].includes(u.state);
 for(const id of ['checkUpdate','installUpdate','postponeUpdate','saveUpdates'])$(id).disabled=!ready||updateBusy||busy;
 $('checkUpdate').disabled=!ready||updateBusy||busy||!!u?.countdown;
 for(const id of ['installUpdate','postponeUpdate'])$(id).disabled=!ready||updateBusy||busy||!u?.available;
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
function syncIdlePreset(kind){const value=$(kind+'After').value;$(kind+'Preset').value=['0','1','2','5','10'].includes(String(value))?String(value):'custom';}
for(const kind of ['dim','off']){
 $(kind+'Preset').onchange=()=>{if($(kind+'Preset').value!=='custom'){$(kind+'After').value=$(kind+'Preset').value;appearanceDirty.add(kind+'After');}else $(kind+'After').focus();};
 $(kind+'After').oninput=()=>{appearanceDirty.add(kind+'After');syncIdlePreset(kind);};
}
function fill(s){
 for(const key of appearanceKeys){
  if(appearanceDirty.has(key))continue;
  if(key==='rotate')$(key).checked=s[key];
  else if(key==='color')$(key).value='#'+s[key].toString(16).padStart(6,'0');
  else $(key).value=s[key]??(key==='dimBrightness'?10:0);
 }
 for(const kind of ['dim','off'])syncIdlePreset(kind);
 $('brightnessValue').textContent=$('brightness').value+'%';
}
function applyStatus(next){state=next;
 if(next.update){const u=next.update;
  const titles={1:'Checking for updates…',2:u.countdown?'Your update is ready to install':'A new update is available',3:'Downloading update',4:'Verifying update',5:'Restarting Tempo — reconnecting automatically',6:'Update needs attention',7:'Preparing to install'};
  $('updateTitle').textContent=titles[u.state]||(u.upToDate?'No new updates — Tempo is up to date':'Ready to check for updates');
  $('updateTitle').classList.toggle('error',u.state===6);
  $('updateVersion').textContent='Installed version: '+u.installed+(u.available?' · Update available: '+u.available:'');
  $('updateStatus').textContent=u.message;
  $('updateCountdown').textContent=u.countdown?'Installing in '+u.countdown+' seconds':u.state===3?u.progress+'% downloaded':u.state===7?'Tempo’s screen will rest during installation. Keep it powered on.':'';
  $('updateCountdown').hidden=!$('updateCountdown').textContent;
  $('updateProgress').hidden=!(u.countdown||u.state===3);
  $('updateProgress').value=u.countdown?Math.max(0,100-u.countdown*100/60):u.progress;
  $('updateProgress').setAttribute('aria-label',u.countdown?'Automatic installation countdown':'Download progress');
  $('updateMessage').textContent='';
  $('releaseNotes').hidden=!u.notes;$('releaseNotes').href=/^https:\/\/github\.com\/tinyrobotcompany\/Tempo\/releases\/tag\/v[0-9.]+$/.test(u.notes)?u.notes:'#';
  if(!updateEdited){$('updateHours').value=u.hours;$('updateAutomatic').checked=u.automatic;for(const [id,n] of [['updateStart',u.start],['updateEnd',u.end]])$(id).value=String(Math.floor(n/60)).padStart(2,'0')+':'+String(n%60).padStart(2,'0');}
  $('credentialStatus').textContent=next.transportConfigured?'Transport key is saved.':'No transport key configured.';
 }

 const loc=state.location;$('savedLocation').textContent=loc.valid?`${loc.name} · ${loc.timezone}`:'No location saved yet.';
 fill(state.settings);
 if(!initialised){initialised=true;
  if(loc.valid)editor.choose(loc);
 }

 updateControls();
}
$('settingsForm').addEventListener('input',markAppearance);
$('settingsForm').addEventListener('change',markAppearance);
$('brightness').oninput=()=>{$('brightnessValue').textContent=$('brightness').value+'%';};
$('settingsForm').onsubmit=async e=>{e.preventDefault();if(savePending||!connection.connected())return;const settings={name:$('name').value,brightness:Number($('brightness').value),theme:Number($('theme').value),font:Number($('font').value),color:parseInt($('color').value.slice(1),16),rotate:$('rotate').checked,dwell:Number($('dwell').value),dimAfter:Number($('dimAfter').value),offAfter:Number($('offAfter').value),dimBrightness:Number($('dimBrightness').value)};
 if(settings.dimAfter&&settings.offAfter&&settings.offAfter<=settings.dimAfter){message('Screen off must be later than dim, or set either to Never.','settingsMessage',true);return;}
 if(new TextEncoder().encode(settings.name).length>48){message('Please use a shorter name.','settingsMessage',true);return;}
 savePending=true;updateControls();message('Saving…','settingsMessage');try{await api('settings',settings);
 // Wait for the UI worker to apply the command and the NVS worker to commit it.
 let saved=false;for(let i=0;i<30;i++){await new Promise(r=>setTimeout(r,300));const s=await api('status');if(Object.keys(settings).every(k=>s.settings[k]===settings[k])&&s.saveState===2){saved=true;for(const key of appearanceKeys)if(appearanceValue(key)===settings[key])appearanceDirty.delete(key);state={...state,settings:s.settings};fill(s.settings);break;}if(s.saveState===3)throw new Error('Could not save. Please retry.');}
 if(!saved)throw new Error('Save not confirmed. Check the display before retrying.');message('SAVED — Your display settings are stored. Idle timers have started.','settingsMessage');
 }catch(e){message(connection.connected()?e.message:'Save not confirmed. Your changes are kept here. Check the display after reconnection before saving again.','settingsMessage',true);}finally{savePending=false;updateControls();}};
for(const b of document.querySelectorAll('[data-view]'))b.onclick=async()=>{if(!connection.connected()||viewPending)return;viewPending=true;const caption=b.textContent;b.textContent='Opening…';b.setAttribute('aria-busy','true');message('Changing the display view…');updateControls();try{await api('view',{view:Number(b.dataset.view)});message('Display view requested.');}catch(e){message(e.message,'message',true);}finally{viewPending=false;b.textContent=caption;b.setAttribute('aria-busy','false');updateControls();}};
$('locate').onclick=async()=>{
 if(!connection.connected()||locatePending)return;
 if(globalThis.isSecureContext&&globalThis.navigator?.geolocation){
  try{locatePending=true;updateControls();await editor.locate();}
  catch(e){message(e.message,'locationMessage',true);}
  finally{locatePending=false;updateControls();}
  return;
 }
 message('Location is unavailable in this browser. Search for your address below, then confirm the map pin.','locationMessage',true);
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
 try{await api('update',{action,...body});message('Request accepted. Waiting for Tempo…','updateMessage');await connection.wake();}
 catch(e){message(e.message,'updateMessage',true);}
 finally{updateBusy=false;for(const id of ['checkUpdate','installUpdate','postponeUpdate','saveUpdates']){$(id).setAttribute('aria-busy','false');}updateControls();}
}
$('checkUpdate').onclick=()=>updateCommand('check');$('installUpdate').onclick=()=>updateCommand('install_now');$('postponeUpdate').onclick=()=>updateCommand('postpone');
$('updateForm').addEventListener('input',()=>{updateEdited=true;});
$('updateForm').onsubmit=e=>{e.preventDefault();const start=minutes($('updateStart').value),end=minutes($('updateEnd').value);if(start===end){message('Choose different start and end times.','updateMessage',true);return;}return updateCommand('configure',{hours:Number($('updateHours').value),automatic:$('updateAutomatic').checked,start,end});};
$('credentialForm').onsubmit=async e=>{e.preventDefault();if(credentialBusy||!connection.connected())return;
 credentialBusy=true;$('saveCredential').disabled=true;$('saveCredential').textContent='Saving…';message('Saving transport key…','credentialMessage');
 try{await api('credential',{key:$('primKey').value});$('primKey').value='';message('Key saved. Transport will test it when refreshing.','credentialMessage');}
 catch(e){message(e.message,'credentialMessage',true);}
 finally{credentialBusy=false;$('saveCredential').textContent='Save transport key';updateControls();}
};
