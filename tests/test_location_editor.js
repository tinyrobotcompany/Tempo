const assert=require('node:assert/strict');
const test=require('node:test');
require('../firmware/main/web/location.js');
class Element {
 constructor(){this.children=[];this.style={};this.value='';this.clientWidth=320;this.clientHeight=240;this.textContent='Search';}
 replaceChildren(...xs){this.children=xs;} append(x){this.children.push(x);}
 setAttribute(){} addEventListener(event,fn){this[event]=fn;} setPointerCapture(){}
 querySelector(){return this.button||(this.button=new Element());}
}
test('location editor integrates lookup, map, name edits and geolocation without a live provider',async()=>{
 const nodes=new Map();const el=id=>{if(!nodes.has(id))nodes.set(id,new Element());return nodes.get(id);};
 global.document={getElementById:el,createElement:()=>new Element()};
 const messages=[],changes=[];let ok=true,features=[];
 global.fetch=async(url,options)=>{assert(url.startsWith('https://data.geopf.fr/geocodage/'));assert.equal(options.credentials,'omit');return{ok,json:async()=>({features})};};
 const editor=StationLocation.editor({message:x=>messages.push(x),onChange:x=>changes.push({...x})});
 await editor.choose({latitude:100,longitude:0});assert.equal(editor.get(),null);
 await editor.choose({latitude:48,longitude:2,name:'Home'});assert.equal(editor.get().name,'Home');
 el('locationName').value='Châtenay';el('locationName').input();assert.equal(editor.get().name,'Châtenay');
 const form=el('searchForm'),event={preventDefault(){}};
 el('address').value='ab';await form.submit(event);assert(messages.at(-1).includes('three'));
 el('address').value='Town';await form.submit(event);assert(messages.at(-1).includes('No results'));
 features=[{geometry:{coordinates:[2.3,48.8]},properties:{city:'Town',label:'Street'}}];
 await form.submit(event);assert.equal(el('results').children.length,1);el('results').children[0].onclick();assert.equal(editor.get().name,'Town');
 assert.equal(form.querySelector().disabled,false);
 ok=false;await form.submit(event);assert(messages.at(-1).includes('unavailable'));
 await editor.choose({latitude:49,longitude:3},true);assert(messages.at(-1).includes('Position selected'));
 ok=true;await editor.choose({latitude:49,longitude:3},true);assert.equal(editor.get().name,'Town');
 const map=el('map');const zoom=map.children.at(-1);zoom.children[0].onclick();zoom.children[1].onclick();
 const pointer={target:{closest:()=>false},clientX:0,clientY:0,pointerId:1};
 map.pointerdown(pointer);map.pointermove({...pointer,clientX:20});map.pointerup({...pointer,clientX:20});
 map.pointercancel();map.keydown({key:'ArrowRight',preventDefault(){}});map.keydown({key:'x'});
 assert(changes.length>3);
 await assert.rejects(editor.locate(),/secure/);
 global.isSecureContext=true;
 Object.defineProperty(global,'navigator',{configurable:true,value:{geolocation:{getCurrentPosition:resolve=>resolve({coords:{latitude:48,longitude:2,accuracy:10}})}}});
 await editor.locate();assert(messages.at(-1).includes('accuracy about 10'));
 // A late reverse lookup must not overwrite a more recent manual selection.
 let finish;global.fetch=()=>new Promise(resolve=>{finish=resolve;});
 const older=editor.choose({latitude:48,longitude:2},true);
 await editor.choose({latitude:50,longitude:4,name:'New home'});
 finish({ok:true,json:async()=>({features})});await older;assert.equal(editor.get().name,'New home');
});
