const assert=require('node:assert/strict');require('../firmware/main/web/location.js');const L=globalThis.StationLocation;
assert(L.validPoint({latitude:0,longitude:0}));assert(!L.validPoint({latitude:91,longitude:0}));assert(!L.validPoint({latitude:NaN,longitude:0}));
for(const url of ['http://192.168.1.34/','http://172.20.10.2/','http://10.0.0.1/'])assert.equal(L.privateReturn(url),url);
for(const url of ['https://example.com/','http://8.8.8.8/','http://evil.local/','http://192.168.1.34.evil.com/','javascript:alert(1)','http://user@192.168.1.2/','http://192.168.1.2/api/location','http://192.168.1.2/?token=oops','http://192.168.1.2/#token=oops'])assert.equal(L.privateReturn(url),null);
for(const [lat,lon]of[[48.8566,2.3522],[-33.86,151.2],[0,0],[85,-179]]){const p=L.project(lat,lon,15),q=L.unproject(p.x,p.y,15);assert(Math.abs(q.latitude-lat)<1e-8);assert(Math.abs(q.longitude-lon)<1e-8);}
assert(new TextEncoder().encode(L.shortName('é'.repeat(50))).length<=63);assert.equal(L.shortName('Town\nName'),'TownName');
const fs=require('node:fs');assert.equal(fs.readFileSync('firmware/main/web/location.js','utf8'),fs.readFileSync('companion/location-helper/location.js','utf8'));
console.log('PASS: location coordinates, private return URLs, map projection and UTF-8 name bounds');

assert.equal(L.shortName('Cha\u0302tenay-Malabry'),'Châtenay-Malabry');
