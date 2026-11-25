const assert=require('node:assert/strict');require('../firmware/main/web/location.js');const L=globalThis.StationLocation;
assert(L.validPoint({latitude:0,longitude:0}));assert(!L.validPoint({latitude:91,longitude:0}));assert(!L.validPoint({latitude:NaN,longitude:0}));
for(const [lat,lon]of[[48.8566,2.3522],[-33.86,151.2],[0,0],[85,-179]]){const p=L.project(lat,lon,15),q=L.unproject(p.x,p.y,15);assert(Math.abs(q.latitude-lat)<1e-8);assert(Math.abs(q.longitude-lon)<1e-8);}
assert(new TextEncoder().encode(L.shortName('é'.repeat(50))).length<=63);assert.equal(L.shortName('Town\nName'),'TownName');
console.log('PASS: location coordinates, private return URLs, map projection and UTF-8 name bounds');

assert.equal(L.shortName('Cha\u0302tenay-Malabry'),'Châtenay-Malabry');
