/* Apple Shortcuts x-callback-url handoff. The shortcut itself has no device secrets. */
(function(root){
'use strict';
const NAME='Weather Station Location';
function parseResult(text){
 if(typeof text!=='string'||text.length>160)return null;
 const parts=text.trim().split(/\r?\n/).map(s=>s.trim());
 if(parts.length!==3||parts[0]!=='WSLOC1')return null;
 const parse=s=>{s=s.replace(/\u2212/g,'-').replace(',','.');return /^[+-]?\d+(?:\.\d+)?$/.test(s)?Number(s):NaN;};
 const latitude=parse(parts[1]),longitude=parse(parts[2]);
 return Number.isFinite(latitude)&&Math.abs(latitude)<=90&&Number.isFinite(longitude)&&Math.abs(longitude)<=180?{latitude,longitude,name:'Home location'}:null;
}
function launch(origin,token,state){
 const returnTo=StationLocation.privateReturn(origin+'/');
 if(!returnTo||!/^[a-f0-9]{32}$/.test(token)||!/^[a-f0-9]{32}$/.test(state))throw new Error('Scan the display’s current QR code before using location.');
 const callback=status=>{const u=new URL(returnTo);u.search=new URLSearchParams({location_state:state,location_status:status});u.hash=new URLSearchParams({token});return u.href;};
 // Custom URL schemes require percent-encoded values, not form encoding:
 // Shortcuts interprets URLSearchParams' '+' spaces as literal name characters.
 const parameters={name:NAME,'x-success':callback('success'),'x-cancel':callback('cancel'),'x-error':callback('error')};
 return 'shortcuts://x-callback-url/run-shortcut?'+Object.entries(parameters).map(([key,value])=>encodeURIComponent(key)+'='+encodeURIComponent(value)).join('&');
}
// The caller must validate location_state with the paired display first.
function consumeVerified(search){
 const q=new URLSearchParams(search);
 if(q.get('location_status')==='cancel')return{error:'Location cancelled. You can try again or search for your address.'};
 if(q.get('location_status')!=='success')return{error:'The shortcut could not return a location. Check location permission, or search for your address.'};
 const point=parseResult(q.get('result'));return point?{point}:{error:'The shortcut returned an unexpected result. Add the supplied shortcut again, or use address search.'};
}
root.StationShortcut={NAME,parseResult,launch,consumeVerified};
})(globalThis);
