const {test}=require('node:test'),assert=require('node:assert/strict'),nav=require('../web/navigator.js');
const origin={latitude:20,longitude:-100,accuracy:5,timestamp:100000};
test('GPS displacement maps meters to Mars cells with explicit rotation',()=>{
 const fix={...origin,latitude:origin.latitude+10/6371008.8*180/Math.PI};
 const t={cell_m:2,cell_x_m:4},anchor={fix:origin,point:[50,50],rotation:0};
 const point=nav.trainingPoint(anchor,fix,t);assert.ok(Math.abs(point[0]-45)<1e-8);assert.equal(point[1],50);
 const rotated=nav.trainingPoint({...anchor,rotation:90},fix,t);assert.ok(Math.abs(rotated[1]-52.5)<1e-8);assert.ok(Math.abs(rotated[0]-50)<1e-8);
 const across=nav.displacement({latitude:0,longitude:179.999},{latitude:0,longitude:-179.999});assert.ok(across.east>200&&across.east<230);
});
test('old, imprecise, future and nonfinite fixes cannot drive guidance',()=>{
 assert.ok(nav.validFix(origin,101000));assert.equal(nav.validFix(origin,116000),false);
 assert.equal(nav.validFix({...origin,accuracy:26},101000),false);assert.equal(nav.validFix({...origin,latitude:NaN},101000),false);
 assert.equal(nav.validFix({...origin,timestamp:110000},100000),false);
});
test('tracking avoids jumps to the return leg and advances through a U turn',()=>{
 const path=[[0,0],[0,100],[0,0]],scale={x:1,y:1};
 assert.equal(nav.nearest([0,5],path,scale,0,35).distance,5);
 assert.equal(nav.nearest([0,95],path,scale,100,35).distance,105);
 assert.equal(nav.nearest([0,5],path,scale,190,35).distance,195);
 assert.equal(nav.nearest([50,40],path,scale,40,35).offset,50);
});
test('phone GPS starts only by click, anchors explicitly, stops stale guidance and clears watch in background',()=>{
 const vm=require('node:vm'),fs=require('node:fs'),elements=new Map(),listeners={};let onFix,cleared=0,requested=0,stops=0,now=100000,sample;
 const el=id=>{if(!elements.has(id))elements.set(id,{value:'0',style:{},dataset:{},textContent:'',disabled:false,click(){this.onclick?.();}});return elements.get(id);};
 const route={outbound:{time_s:20,legs:[{points:[[50,50],[30,50]]}],points:[[50,50],[30,50]]},return:{time_s:20,points:[[30,50],[50,50]]}};
 const data={safe:route,fast:route},terrain={site:'glen-torridon',cell_m:1,cell_x_m:1,grid:{rows:100,cols:100}};
 const context={terrain,data,profile:'safe',path:[[50,50],[30,50],[50,50]],sample:{point:[50,50],distance:0,total:40,index:1},running:false};
 const window={addEventListener(name,cb){listeners[name]=cb;},MissionNavigation:require('../web/navigation.js'),MissionPlayback:{pause(){}},MissionGuidance:{stop(){stops++;}}};
 const document={hidden:false,getElementById:el,addEventListener(name,cb){listeners[name]=cb;}};
 const sandbox={window,document,navigator:{geolocation:{watchPosition(cb){requested++;onFix=cb;return 7;},clearWatch(id){assert.equal(id,7);cleared++;}}},Date:class extends Date{static now(){return now;}},draw(){sample=window.FieldNavigator.track(context);window.FieldNavigator.update();}};
 vm.runInNewContext(fs.readFileSync('web/navigator.js','utf8'),sandbox);sandbox.draw();assert.equal(requested,0);
 el('navgps').onclick();assert.equal(requested,1);assert.equal(window.FieldNavigator.guidanceSample(context.sample),null);
 const send=latitude=>onFix({coords:{latitude,longitude:-100,accuracy:5},timestamp:now});send(20);assert.equal(window.FieldNavigator.isTracking(),false);
 el('navanchor').onclick();assert.equal(window.FieldNavigator.isTracking(),true);
 now+=1000;send(20+10/6371008.8*180/Math.PI);assert.ok(Math.abs(sample.distance-10)<1e-7);
 now+=16000;sandbox.draw();assert.equal(window.FieldNavigator.isTracking(),false);assert.equal(window.FieldNavigator.guidanceSample(context.sample),null);assert.ok(stops>0);
 document.hidden=true;listeners.visibilitychange();assert.equal(cleared,1);assert.equal(el('navgps').disabled,false);
});
test('initial travel course follows the chosen Earth rotation and changes on return',()=>{
 assert.deepEqual(nav.course([[0,0],[-10,0]],{point:[0,0],index:1},{x:1,y:1},90),{degrees:90,cardinal:'E'});
 assert.deepEqual(nav.course([[0,0],[-10,0],[0,0]],{point:[-10,0],index:1},{x:1,y:1},0),{degrees:180,cardinal:'S'});
});
