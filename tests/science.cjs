const {test}=require('node:test'),assert=require('node:assert/strict');
const {triangleColor,buildProfile}=require('../web/science.js');
const terrain={rows:[0,10],cols:[0,10],elevations:[[-20,-10],[-10,0]],cell_m:4,cell_x_m:3,
  slopes:[[0,10],[20,null]],statistics:{elevation_min_m:-20,elevation_max_m:0}};
test('scientific layers produce colors without modifying elevations',()=>{
  const before=JSON.stringify(terrain),ids=[[0,0],[1,0],[0,1]];
  const natural=triangleColor(terrain,ids,'natural');
  assert.match(natural,/^rgb\(/);
  assert.notEqual(triangleColor(terrain,ids,'elevation'),natural);
  assert.notEqual(triangleColor(terrain,ids,'slope'),natural);
  assert.equal(triangleColor(terrain,[[1,0],[1,1],[0,1]],'slope'),'rgb(70,70,70)');
  assert.equal(JSON.stringify(terrain),before);
});
test('elevation profile uses both physical axis scales and preserves unknown heights',()=>{
  const p=buildProfile([[0,0],[0,10],[10,10]],terrain,point=>point[0]===10?null:-20+point[1]);
  assert.equal(p.total,70);assert.equal(p.low,-20);assert.equal(p.high,-10);
  assert.equal(p.points[2].height,null);assert.equal(p.points[1].distance,30);
  assert.deepEqual(buildProfile([],terrain,()=>0).points,[]);
});
