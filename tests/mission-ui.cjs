const {test}=require('node:test'),assert=require('node:assert/strict'),vm=require('node:vm'),fs=require('node:fs');
test('science intent, manual zones and notebook persist by region without changing another mission',()=>{
 const elements=new Map(),saved=new Map(),listeners={};let changes=0;
 const el=id=>{if(!elements.has(id))elements.set(id,{value:'',textContent:'',children:[],appendChild(child){this.children.push(child);},replaceChildren(){this.children=[];}});return elements.get(id);};
 const state={region:'glen-torridon'},window={FieldPoints:{namedPoints:()=>[{point:[3,4],name:'Science target'}]},localStorage:{getItem:k=>saved.get(k)||null,setItem:(k,v)=>saved.set(k,v)},addEventListener:(name,fn)=>listeners[name]=fn,dispatchEvent:event=>listeners[event.type]?.(event)};
 const context={window,state,document:{getElementById:el,createElement:()=>({appendChild(){}})},CustomEvent:class{constructor(type,options){this.type=type;this.detail=options.detail;}},missionChanged(){changes++;},draw(){}};
 vm.runInNewContext(fs.readFileSync('web/mission.js','utf8'),context);
 const terrain={site:'glen-torridon',grid:{rows:11,cols:11},cell_m:4,cell_x_m:4};window.ScienceMission.update(terrain);
 el('missiongoal').value='Compare veins';el('missionmobility').value='.5';el('missionmobility').onchange();assert.equal(window.ScienceMission.request().mobility_factor,.5);
 el('obstacleradius').value='10';el('obstacleplace').onclick();assert.ok(window.ScienceMission.handlePick([3,5]));assert.equal(window.ScienceMission.exclusions()[0].kind,'manual_obstacle');
 el('notepoint').value='0';el('notetext').value='Observed a pale vein';el('noteadd').onclick();assert.equal(window.ScienceMission.notes()[0].source,'user_observation_unverified');assert.equal(changes,2);
 state.region='jezero';window.ScienceMission.update({...terrain,site:'jez_dtm_1m'});assert.equal(window.ScienceMission.notes().length,0);assert.equal(window.ScienceMission.exclusions().length,0);
 state.region='glen-torridon';window.ScienceMission.update(terrain);assert.equal(window.ScienceMission.notes().length,1);assert.equal(window.ScienceMission.exclusions().length,1);assert.equal(window.ScienceMission.request().science_goal,'Compare veins');
 el('obstacleplace').onclick();window.dispatchEvent({type:'pick-mode',detail:'custom'});assert.equal(window.ScienceMission.handlePick([4,4]),false);
});
