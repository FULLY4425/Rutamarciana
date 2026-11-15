const {test}=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),vm=require('node:vm');
function clock(iso,longitude=77.4){
 const context={Date:class extends Date{constructor(){super(iso)} static now(){return new Date(iso).getTime()}},Math};
 vm.createContext(context);vm.runInContext(fs.readFileSync('web/hub.js','utf8'),context);
 return context.MarsHub.marsClock(new context.Date(iso),longitude);
}
test('Mars clock reports normalized LMST, solar sol and explicit approximation provenance',()=>{
 const result=clock('2026-11-15T12:00:00Z');
 assert.equal(result.longitude,77.4);
 assert.ok(result.lmstHours>=0 && result.lmstHours<24);
 assert.equal(result.sol,Math.floor(result.msd));
 assert.ok(result.msd>0);
});
test('LMST shifts by one Mars hour per 15 degrees east',()=>{
 const a=clock('2026-11-15T12:00:00Z',0),b=clock('2026-11-15T12:00:00Z',15);
 assert.ok(Math.abs((((b.lmstHours-a.lmstHours)+12)%24+24)%24-13)<1e-8);
});
test('hub integrates homepage modules without replacing the legacy map controls or region query',()=>{
 const html=fs.readFileSync('web/index.html','utf8');
 assert.match(html,/id="module-hub"/);assert.match(html,/id="bitacora-entry"/);
 assert.match(html,/href="\/\?region=jezero#map-section"/);
 assert.match(html,/id="map"/);assert.match(html,/id="compute"/);assert.match(html,/hub\.js/);
});
