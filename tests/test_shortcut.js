const assert=require('node:assert/strict');require('../firmware/main/web/location.js');require('../firmware/main/web/shortcut.js');
const S=StationShortcut;
assert.deepEqual(S.parseResult('WSLOC1\n48.81\n2.22'),{latitude:48.81,longitude:2.22,name:'Home location'});
assert.deepEqual(S.parseResult('WSLOC1\r\n-33,86\r\n151,2'),{latitude:-33.86,longitude:151.2,name:'Home location'});
for(const x of ['', '48.81,2.22','WSLOC1\n\n0','WSLOC1\n91\n2','WSLOC1\nNaN\n2','WSLOC1\n1e2\n0','WSLOC1\n1\n2\n3'])assert.equal(S.parseResult(x),null);
const token='a'.repeat(32),state='b'.repeat(32),url=new URL(S.launch('http://172.20.10.2',token,state));
// Shortcuts percent-decodes custom-scheme queries; it does not decode form '+' spaces.
const nativeParams=Object.fromEntries(url.search.slice(1).split('&').map(pair=>{const i=pair.indexOf('=');return [decodeURIComponent(pair.slice(0,i)),decodeURIComponent(pair.slice(i+1))];}));
assert.equal(nativeParams.name,'Weather Station Location');
assert(url.href.includes('name=Weather%20Station%20Location'));
assert(!url.search.includes('+'));
for(const key of ['x-success','x-cancel','x-error']){const callback=new URL(nativeParams[key]);assert.equal(callback.origin,'http://172.20.10.2');assert.equal(callback.searchParams.get('location_state'),state);assert.equal(new URLSearchParams(callback.hash.slice(1)).get('token'),token);}
assert.equal(url.protocol,'shortcuts:');assert.equal(url.searchParams.get('name'),'Weather Station Location');
const success=new URL(url.searchParams.get('x-success'));assert.equal(success.origin,'http://172.20.10.2');assert(!success.search.includes(token));
success.searchParams.set('result','WSLOC1\n48.81\n2.22');
assert(S.consumeVerified(success.search).point);
success.searchParams.set('location_status','cancel');assert(S.consumeVerified(success.search).error);
success.searchParams.set('location_status','error');assert(S.consumeVerified(success.search).error);
assert.throws(()=>S.launch('http://example.com',token,state));
console.log('PASS: Shortcuts callback format, comma decimals, coordinate limits, hotspot return, session matching and expiry');
