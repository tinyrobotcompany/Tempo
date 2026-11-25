'use strict';
const $=id=>document.getElementById(id),L=StationLocation,parameters=new URLSearchParams(location.hash.slice(1));
const returnTo=L.privateReturn(parameters.get('return')),token=parameters.get('token');
history.replaceState(null,'',location.pathname);
const validSession=returnTo&&/^[a-f0-9]{32}$/.test(token||'');
const message=text=>{$('locationMessage').textContent=text;};
const editor=L.editor({message,onChange:()=>{$('saveLocation').disabled=!validSession;}});
if(!validSession){message('Open this page using “Use my iPhone location” in your display’s control panel.');$('locate').disabled=true;}
$('locate').onclick=async()=>{$('locate').disabled=true;try{await editor.locate();}catch(e){message(e.code===1?'Location permission was declined. You can use address search instead.':e.code===3?'Positioning timed out. Try again near a window, or search for your address.':e.message||'Position unavailable. Try address search.');}finally{$('locate').disabled=!validSession;}};
$('saveLocation').onclick=()=>{if(!validSession||!L.validPoint(editor.get()))return;const p=editor.get(),target=new URL(returnTo);target.hash=new URLSearchParams({token,latitude:p.latitude.toFixed(6),longitude:p.longitude.toFixed(6),name:L.shortName(p.name)}).toString();location.assign(target.href);};
