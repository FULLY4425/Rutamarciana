const {test}=require('node:test'),assert=require('node:assert/strict'),{summarize}=require('../web/chemistry.js');
test('chemical symbols use observed target medians, retain missing data and do not interpolate',()=>{
 const data=[{target_id:'a',lat:1,lon:2,oxides:{CaO:{wt_percent:0}}},{target_id:'a',lat:1,lon:2,oxides:{CaO:{wt_percent:10}}},{target_id:'b',lat:3,lon:4,oxides:{CaO:{wt_percent:null}}}];
 const before=JSON.stringify(data),result=summarize(data,'CaO');assert.equal(result.length,1);assert.equal(result[0].value,5);assert.equal(result[0].count,2);assert.equal(result[0].lat,1);assert.equal(JSON.stringify(data),before);assert.deepEqual(summarize(data,'FeOT'),[]);
});
