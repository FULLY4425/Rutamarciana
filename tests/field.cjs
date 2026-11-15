const {test}=require('node:test'),assert=require('node:assert/strict');
const points=require('../web/points.js'),ops=require('../web/operations.js'),actors=require('../web/actors.js'),{affine}=require('../web/texture.js');
test('Glen coordinates round trip and reject points outside the crop',()=>{
 const meta=require('../assets/glen-torridon/routes.json'),t={...meta,spatial:meta.spatial};
 const p=[30,40],geo=points.gridToGeo(p,t);assert.deepEqual(points.geoToGrid(geo.lat,geo.lon,t),p);assert.equal(points.geoToGrid(80,0,t),null);
});
test('deviation uses segment distance in physical meters and preserves remaining science/home',()=>{
 assert.equal(ops.distanceToRoute([4,5],[[0,0],[0,10]],{x:3,y:4}),16);
 const data={start:[0,0],return_to:[1,0],science_waypoints:[[2,0],[4,0]],safe:{outbound:{distance_m:60,points:[[0,0],[6,0]],legs:[{distance_m:20},{distance_m:20},{distance_m:20}]}}};
 assert.deepEqual(ops.remaining(data,'safe',{distance:25}),{home:[1,0],end:[6,0],stops:[[4,0]]});
 assert.deepEqual(ops.remaining(data,'safe',{distance:70}),{home:[1,0],end:[1,0],stops:[]});
});
test('texture maps all triangle vertices and astronaut geometry preserves human scale',()=>{
 assert.deepEqual(affine([[0,0],[1,0],[0,1]],[[3,4],[5,4],[3,7]]),[2,0,0,3,3,4]);
 const body=actors.geometry([0,0,0],0,0),heights=body.filter((_,i)=>i%8===1);assert.ok(Math.max(...heights)<1.8&&Math.max(...heights)>1.7);
});
