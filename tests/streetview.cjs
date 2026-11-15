const {test}=require('node:test');
const assert=require('node:assert/strict');
const {mesh,heading,alignedTexture,surfaceHeight}=require('../web/streetview.js');
const terrain={rows:[0,1],cols:[0,1],elevations:[[0,0],[0,10]],cell_m:4,cell_x_m:3,grid:{rows:2,cols:2},site:'synth'};

test('surface mesh preserves physical scale, normals and triangle count',()=>{
  const data=mesh(terrain);
  assert.equal(data.vertices.length,32);assert.equal(data.indices.length,6);
  assert.deepEqual(data.vertices.slice(24,27),[3,10,4]);
  for(let i=0;i<data.vertices.length;i+=8)assert.ok(Math.abs(Math.hypot(...data.vertices.slice(i+3,i+6))-1)<1e-8);
  assert.equal(surfaceHeight(terrain,[0.5,0.5]),0);
  assert.equal(surfaceHeight(terrain,[0.75,0.75]),5);
  assert.equal(surfaceHeight(terrain,[1,1]),10);
});
test('NoData is not turned into fabricated terrain or route segments',()=>{
  const missing={...terrain,elevations:[[null,0],[0,10]]};
  assert.equal(mesh(missing).indices.length,3);
  assert.equal(surfaceHeight(missing,[0.1,0.1]),null);
  assert.equal(surfaceHeight(missing,[0.9,0.9]),8);
  assert.equal(surfaceHeight(missing,[-1,0]),null);
  assert.equal(surfaceHeight(missing,[3,0]),null);
});
test('camera bearings follow north-up coordinates in meters',()=>{
  assert.equal(heading([1,0],[0,0],{x:3,y:4}),0);
  assert.equal(heading([0,0],[0,1],{x:3,y:4}),Math.PI/2);
  assert.equal(heading([0,0],[1,0],{x:3,y:4}),Math.PI);
});
test('HiRISE texture requires matching real DEM footprint and source window',()=>{
  const ref={crs:'ESRI:103885',bounds_m:[1,2,3,4],dem_source_window:{row:10,col:20,width:30,height:40}};
  const real={...terrain,site:'jez_dtm_1m',spatial:{crs:ref.crs,bounds:ref.bounds_m,source_window:ref.dem_source_window}};
  assert.equal(alignedTexture(real,ref),true);
  assert.equal(alignedTexture({...real,site:'synth'},ref),false);
  assert.equal(alignedTexture({...real,spatial:{...real.spatial,bounds:[1,2,3,5]}},ref),false);
  assert.equal(alignedTexture({...real,spatial:{...real.spatial,source_window:{...ref.dem_source_window,row:11}}},ref),false);
  assert.equal(alignedTexture(real,null),false);
});

test('surface adapter places eye above rendered ground, steps and closes without changing route',()=>{
  const vm=require('node:vm'),fs=require('node:fs');
  const elements=new Map(),events=[],draws=[],camera=[];
  const gl=new Proxy({
    getShaderParameter:()=>true,getProgramParameter:()=>true,getAttribLocation:()=>0,getUniformLocation:(_,key)=>key,
    uniform3f:(name,...values)=>{if(name==='camera')camera.push(values);},
    drawElements:(...args)=>draws.push(args),
  },{get:(obj,key)=>obj[key] || (()=>{})});
  function element(id){if(!elements.has(id))elements.set(id,{disabled:false,hidden:true,style:{},setAttribute(){},focus(){},addEventListener(){},getContext:()=>gl,getBoundingClientRect:()=>({width:800,height:600})});return elements.get(id);}
  const window={addEventListener(){},dispatchEvent:event=>events.push(event)};
  const sandbox={window,document:{getElementById:element},fetch:()=>new Promise(()=>{}),Image:class{},CustomEvent:class{constructor(type,options){this.type=type;this.detail=options?.detail;}}};
  vm.runInNewContext(fs.readFileSync('web/streetview.js','utf8'),sandbox);
  const sample={point:[0.5,0.5],distance:1,total:10};
  window.SurfaceView.update({terrain,path:[[0,0],[1,1]],sample,ahead:{point:[0.9,0.9]}});
  element('surfaceopen').onclick();
  assert.equal(window.SurfaceView.isOpen(),true);assert.ok(draws.length>0);
  assert.deepEqual(camera.at(-1),[1.5,1.7,2]);
  element('surfaceforward').onclick();assert.equal(events.at(-1).detail,5);
  element('surfaceback').onclick();assert.equal(events.at(-1).detail,-5);
  window.SurfaceView.update({terrain,path:[],sample:null});
  assert.equal(window.SurfaceView.isOpen(),false);
  assert.equal(events.at(-1).type,'surface-close');
  assert.equal(element('surfaceopen').disabled,true);
});
