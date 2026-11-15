const {test}=require('node:test'),assert=require('node:assert/strict');
const vm=require('node:vm'),fs=require('node:fs');
function fixture(){
 const elements=new Map();const drawing=new Proxy({measureText:()=>({width:10})},{get:(obj,key)=>obj[key]||(()=>{})});
 function element(id){if(!elements.has(id))elements.set(id,{value:id==='trackroute'?'safe':id==='terraindetail'?'75':id==='terrainpalette'?'natural':'0',style:{},dataset:{},setAttribute(){},removeAttribute(){},addEventListener(){},appendChild(){},getContext:()=>drawing,width:1000,height:1000,textContent:''});return elements.get(id);}
 const context={document:{getElementById:element,querySelector:element,createElement:element},window:{addEventListener(){},history:{replaceState(){}},MissionGuidance:{stop(){},update(){}}},Image:class{},fetch:()=>new Promise(()=>{}),location:{search:''},URLSearchParams,requestAnimationFrame(){},setTimeout(){}};
 vm.createContext(context);for(const file of ['web/app.js','web/terrain.js'])vm.runInContext(fs.readFileSync(file,'utf8'),context);
 return {context,element,run:code=>vm.runInContext(code,context)};
}
function terrain(region){return {site:region==='jezero'?'jez_dtm_1m':'mola-global',region,grid:{rows:81,cols:81},rows:[0,80],cols:[0,80],elevations:[[0,0],[0,0]],cell_m:5000,cell_x_m:5000,spatial:{center_latitude_deg:0,center_longitude_deg:0}};}
function response(payload){return {ok:true,json:async()=>payload};}
function payload(url,region){return url.includes('/api/terrain?')?terrain(region):{routes:{}};}
test('region selection atomically resets mission and scopes subsequent API requests',async()=>{
 const f=fixture();f.context.fetch=async url=>response(payload(url,'mola:90:0'));
 await f.run("switchRegion('mola:90:0')");
 assert.equal(f.run('state.region'),'mola:90:0');assert.equal(f.run('state.start'),null);
 assert.equal(f.run('state.grid.rows'),81);assert.equal(f.run('state.cellM'),5000);
 assert.match(f.run("apiURL('/api/route')"),/region=mola%3A90%3A0/);
 assert.equal(f.element('compute').disabled,false);assert.equal(f.element('terraindetail').disabled,false);
});
test('an older region response cannot overwrite the newest selection',async()=>{
 const f=fixture(),pending=[];
 f.context.fetch=url=>url.includes('mola%3A1%3A1')?new Promise(resolve=>pending.push(()=>resolve(response(payload(url,'mola:1:1'))))):Promise.resolve(response(payload(url,'mola:2:2')));
 const first=f.run("switchRegion('mola:1:1')");
 await f.run("switchRegion('mola:2:2')");pending.forEach(resolve=>resolve());await first;
 assert.equal(f.run('state.region'),'mola:2:2');assert.equal(f.run('terrain.region'),'mola:2:2');
});
test('late detail loads from the previous dataset are discarded',async()=>{
 const f=fixture();let finish;
 f.context.fetch=url=>new Promise(resolve=>{finish=()=>resolve(response(terrain('jezero')));});
 const previous=f.run('loadTerrain()');
 f.context.fetch=async url=>response(payload(url,'mola:3:3'));
 await f.run("switchRegion('mola:3:3')");finish();await previous;
 assert.equal(f.run('terrain.region'),'mola:3:3');
});
