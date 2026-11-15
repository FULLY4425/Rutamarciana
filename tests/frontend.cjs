// Run with node tests/frontend.cjs; no browser dependencies required.
const vm=require('node:vm'),fs=require('node:fs'),assert=require('node:assert/strict');
const elements=new Map();
const ctx=new Proxy({measureText:()=>({width:10})},{get:(o,k)=>o[k]||(()=>{})});
function element(id){if(!elements.has(id))elements.set(id,{value:id==='trackroute'?'safe':'0',style:{},dataset:{},addEventListener(){},appendChild(){},removeAttribute(){},getContext:()=>ctx,width:1000,height:1000,textContent:'',getBoundingClientRect:()=>({left:0,top:0,width:1000,height:1000})});return elements.get(id);}
const sandbox={console,document:{getElementById:element,querySelector:element,createElement:()=>element('new')},Image:class{},window:{},fetch:()=>new Promise(()=>{}),location:{search:''},URLSearchParams,requestAnimationFrame(){},setTimeout(){}};
vm.createContext(sandbox);
for(const file of ['web/app.js','web/terrain.js'])vm.runInContext(fs.readFileSync(file,'utf8'),sandbox);
vm.runInContext(`
terrain={rows:[0,10],cols:[0,10],elevations:[[0,10],[10,20]],base:0};
state.grid={rows:11,cols:11};state.cellM=4;state.cellXM=3;
if(elevation([5,5])!==10)throw Error('Bilinear elevation');
const trip=samplePath([[0,0],[0,10],[10,10]],0.5);
if(trip.total!==70 || trip.distance!==35 || trip.point[0]!==1.25)throw Error('Physical route distance');
state.data={safe:{points:[[0,0],[0,10]],return:{points:[[0,10],[0,0]]}}};
if(trackingPath().length!==4)throw Error('Return omitted');
state.start=null;state.end=null;state.stops=[];state.nStops=1;
selectPoint([0,0]);selectPoint([10,10]);selectPoint([5,5]);
if(state.data!==null || state.stops.length!==1)throw Error('Point selection invalidation');
terrain.elevations[0][0]=null;
if(elevation([5,5])!==null)throw Error('NoData interpolation');
`,sandbox);
assert.equal(Number(elements.get('progress').value),0);
console.log('Frontend: interpolation, physical distances, return path, selection and NoData passed.');
