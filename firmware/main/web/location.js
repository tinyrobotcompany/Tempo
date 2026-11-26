/* Location editor for the HTTPS device panel. */
(function(root){
'use strict';
const validPoint=p=>p&&Number.isFinite(p.latitude)&&Math.abs(p.latitude)<=90&&Number.isFinite(p.longitude)&&Math.abs(p.longitude)<=180;
function shortName(value){let out='';for(const c of String(value||'Home location').normalize('NFC').replace(/[\x00-\x1f\x7f]/g,'')){if(new TextEncoder().encode(out+c).length>63)break;out+=c;}return out||'Home location';}
function project(lat,lon,z){const n=256*2**z,s=Math.sin(Math.max(-85.05112878,Math.min(85.05112878,lat))*Math.PI/180);return{x:(lon+180)/360*n,y:(.5-Math.log((1+s)/(1-s))/(4*Math.PI))*n};}
function unproject(x,y,z){const n=256*2**z;return{latitude:Math.atan(Math.sinh(Math.PI*(1-2*Math.max(0,Math.min(n,y))/n)))*180/Math.PI,longitude:((x/n*360)%360+360)%360-180};}
async function lookup(path,params){const controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),10000);try{
 const response=await fetch('https://data.geopf.fr/geocodage/'+path+'?'+new URLSearchParams(params),{signal:controller.signal,credentials:'omit',referrerPolicy:'origin'});
 if(!response.ok)throw new Error('Address service unavailable. Try again or use the map.');
 return await response.json();
 }finally{clearTimeout(timeout);}}
async function search(query){const data=await lookup('search',{q:query,index:'address',limit:'5'});return(data.features||[]).map(f=>({latitude:f.geometry?.coordinates?.[1],longitude:f.geometry?.coordinates?.[0],name:shortName(f.properties?.city||f.properties?.name),label:f.properties?.label||f.properties?.name||'Address'})).filter(validPoint);}
async function reverse(p){const data=await lookup('reverse',{lat:p.latitude,lon:p.longitude,index:'address',limit:'1'});const props=data.features?.[0]?.properties;return shortName(props?.city||props?.name||'Home location');}
function map(element,changed){let point=null,z=15,drag=null;const tiles=document.createElement('div');tiles.className='tiles';element.replaceChildren(tiles);
 const pin=document.createElement('span');pin.className='pin';pin.textContent='●';element.append(pin);
 const attr=document.createElement('div');attr.className='attribution';const credit=document.createElement('a');credit.href='https://www.openstreetmap.org/copyright';credit.target='_blank';credit.rel='noopener';credit.textContent='© OpenStreetMap contributors';attr.append(credit);element.append(attr);
 const zoom=document.createElement('div');zoom.className='zoom';for(const [caption,step]of[['+',1],['−',-1]]){const b=document.createElement('button');b.type='button';b.textContent=caption;b.setAttribute('aria-label',step>0?'Zoom in':'Zoom out');b.onclick=()=>{z=Math.max(3,Math.min(18,z+step));render();};zoom.append(b);}element.append(zoom);
 element.tabIndex=0;element.setAttribute('aria-label','Location map. Drag or use arrow keys to move the centre pin.');
 function render(){if(!point)return;const c=project(point.latitude,point.longitude,z),w=element.clientWidth,h=element.clientHeight,n=2**z;tiles.replaceChildren();tiles.style.transform='';
 for(let y=Math.floor((c.y-h/2)/256);y<=Math.floor((c.y+h/2)/256);y++)for(let x=Math.floor((c.x-w/2)/256);x<=Math.floor((c.x+w/2)/256);x++){if(y<0||y>=n)continue;const img=document.createElement('img');img.alt='';img.draggable=false;img.referrerPolicy='origin';img.src=`https://tile.openstreetmap.org/${z}/${((x%n)+n)%n}/${y}.png`;img.style.left=(x*256-c.x+w/2)+'px';img.style.top=(y*256-c.y+h/2)+'px';tiles.append(img);}}
 element.addEventListener('pointerdown',e=>{if(!point||e.target.closest('button,a'))return;drag={x:e.clientX,y:e.clientY,id:e.pointerId};element.setPointerCapture(e.pointerId);});
 element.addEventListener('pointermove',e=>{if(drag)tiles.style.transform=`translate(${e.clientX-drag.x}px,${e.clientY-drag.y}px)`;});
 element.addEventListener('pointerup',e=>{if(!drag)return;const c=project(point.latitude,point.longitude,z),dx=e.clientX-drag.x,dy=e.clientY-drag.y;drag=null;if(Math.hypot(dx,dy)>3){point=unproject(c.x-dx,c.y-dy,z);changed(point);}render();});
 element.addEventListener('pointercancel',()=>{drag=null;render();});
 element.addEventListener('keydown',e=>{if(!point)return;const moves={ArrowLeft:[-30,0],ArrowRight:[30,0],ArrowUp:[0,-30],ArrowDown:[0,30]};if(!moves[e.key])return;e.preventDefault();const c=project(point.latitude,point.longitude,z),[x,y]=moves[e.key];point=unproject(c.x+x,c.y+y,z);changed(point);render();});
 if(typeof ResizeObserver!=='undefined')new ResizeObserver(render).observe(element);
 return{set(p){if(validPoint(p)){point={...p};render();}}};
}
function editor({message,onChange}){
 let candidate=null,revision=0;const el=id=>document.getElementById(id),m=map(el('map'),p=>choose(p,true));
 async function choose(p,resolve=false){if(!validPoint(p))return;const rev=++revision;candidate={latitude:p.latitude,longitude:p.longitude,name:shortName(p.name)};el('locationName').value=candidate.name;el('coordinates').textContent=`${p.latitude.toFixed(6)}, ${p.longitude.toFixed(6)}`;m.set(candidate);onChange(candidate);if(resolve){try{const name=await reverse(p);if(rev===revision){candidate.name=name;el('locationName').value=name;onChange(candidate);}}catch{if(rev===revision)message('Position selected. You can enter the place name below.');}}}
 el('locationName').addEventListener('input',()=>{if(candidate){revision++;candidate.name=shortName(el('locationName').value);onChange(candidate);}});
 let searchRevision=0,searchPending=false;el('searchForm').addEventListener('submit',async e=>{e.preventDefault();if(searchPending)return;const q=el('address').value.trim();if(q.length<3){message('Enter at least three characters.');return;}const rev=++searchRevision;searchPending=true;const button=el('searchForm').querySelector('button');const caption=button?.textContent;if(button){button.disabled=true;button.textContent='Searching…';button.setAttribute('aria-busy','true');}message('Searching addresses…');el('results').replaceChildren();try{const results=await search(q);if(rev!==searchRevision)return;message(results.length?'Choose an address, then check the map.':'No results. Try a fuller address.');for(const p of results){const b=document.createElement('button');b.type='button';b.textContent=p.label;b.onclick=()=>{choose(p);el('results').replaceChildren();message('Check the pin, then confirm your location.');};el('results').append(b);}}catch(e){if(rev===searchRevision)message(e.message);}finally{searchPending=false;if(button){button.disabled=false;button.textContent=caption;button.setAttribute('aria-busy','false');}}});
 return{choose,get:()=>candidate,async locate(){if(!root.isSecureContext||!navigator.geolocation)throw new Error('Automatic positioning requires the secure location page.');message('Finding your position…');const result=await new Promise((resolve,reject)=>navigator.geolocation.getCurrentPosition(resolve,reject,{enableHighAccuracy:true,timeout:20000,maximumAge:0}));await choose({latitude:result.coords.latitude,longitude:result.coords.longitude},true);message(`Position found (accuracy about ${Math.round(result.coords.accuracy)} m). Check the pin before saving.`);}};
}
root.StationLocation={validPoint,shortName,project,unproject,editor};
})(globalThis);
