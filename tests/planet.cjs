const {test}=require('node:test'),assert=require('node:assert/strict');
const {project,unproject}=require('../web/planet.js');
test('globe picking inverts visible projected coordinates in both hemispheres',()=>{
  for(const [lat,lon,yaw,pitch] of [[18,77,1,.2],[-40,-60,-1,-.5],[85,120,2,1.3],[-85,0,0,-1.3]]){
    const [x,y,z]=project(lat,lon,yaw,pitch);assert.ok(z>0);
    const point=unproject(x,y,yaw,pitch);
    assert.ok(Math.abs(point.lat-lat)<1e-8);assert.ok(Math.abs(point.lon-lon)<1e-8);
  }
});
test('outside the globe is not a coordinate selection',()=>{assert.equal(unproject(1,1,0,0),null);});
