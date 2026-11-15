// node --test tests/navigation.cjs
const {test}=require('node:test');
const assert=require('node:assert/strict');
const vm=require('node:vm'),fs=require('node:fs');
const navigation=require('../web/navigation.js');
const scale={x:4,y:4};

test('right and left directions use north-up grid, include return and physical distances',()=>{
  const route={points:[[30,0],[0,0],[0,30]],return:{points:[[0,30],[0,0],[30,0]]}};
  const plan=navigation.build(route,scale);
  assert.deepEqual(plan.steps.map(s=>s.kind),['depart','right','destination','left','home']);
  assert.deepEqual(plan.steps.map(s=>s.at),[0,120,240,360,480]);
  assert.equal(navigation.atPosition(plan,90).distance,30);
  assert.equal(navigation.atPosition(plan,240).step.kind,'destination');
  assert.equal(navigation.atPosition(plan,480).step.kind,'home');
  assert.equal(navigation.atPosition(plan,500).remaining,0);
});

test('science stops are kept in order even on a straight route',()=>{
  const plan=navigation.build({outbound:{legs:[{points:[[0,0],[0,10]]},{points:[[0,10],[0,30]]}]},return:{points:[[0,30],[0,0]]}},{x:3,y:4});
  assert.deepEqual(plan.steps.map(s=>s.kind),['depart','science','destination','home']);
  assert.equal(plan.steps[1].at,30);
  assert.match(plan.steps[1].text,/S1/);
  assert.equal(plan.total,180);
});

test('grid stair steps do not create a turn announcement for every cell',()=>{
  const points=Array.from({length:100},(_,i)=>[Math.floor(i/2),Math.ceil(i/2)]);
  assert.equal(navigation.build({points},scale).steps.length,2);
  assert.equal(navigation.atPosition(navigation.build(null,scale),0),null);
});

test('duplicate points remain finite and a reversal is a U-turn',()=>{
  const plan=navigation.build({points:[[0,0],[0,0],[0,30],[0,0]]},scale);
  assert.equal(plan.total,240);
  assert.equal(plan.steps[1].kind,'uturn');
});

function controller(support=true) {
  const elements=new Map(),spoken=[];let cancellations=0;
  function element(id){if(!elements.has(id))elements.set(id,{textContent:'',dataset:{},children:[],setAttribute(k,v){this[k]=v;},replaceChildren(){this.children=[];},appendChild(child){this.children.push(child);}});return elements.get(id);}
  const speech={cancel(){cancellations++;},getVoices(){return [{lang:'es-MX',name:'Español'}];},speak(u){spoken.push(u);}};
  const window={MissionNavigation:navigation,addEventListener(){}};
  if(support){window.speechSynthesis=speech;window.SpeechSynthesisUtterance=class{constructor(text){this.text=text;}};}
  const sandbox={window,document:{getElementById:element,createElement:()=>({})}};
  vm.runInNewContext(fs.readFileSync('web/guidance.js','utf8'),sandbox);
  return {guidance:window.MissionGuidance,element,spoken,cancellations:()=>cancellations};
}
const data={safe:{points:[[30,0],[0,0],[0,30]],return:{points:[[0,30],[30,0]]}}};

test('voice requires activation, speaks once per cue, warns at 30m and cancels on pause',()=>{
  const c=controller();
  const update=(distance,playing=true)=>c.guidance.update(data,'safe',{distance},playing,scale);
  update(0);assert.equal(c.spoken.length,0);
  c.element('voice').onclick();assert.equal(c.spoken.length,1);
  assert.equal(c.spoken[0].lang,'es-MX');
  update(10);update(20);assert.equal(c.spoken.length,1);
  update(95);assert.equal(c.spoken.length,2);assert.match(c.spoken[1].text,/25 m/);
  update(120);assert.equal(c.spoken.length,3);assert.match(c.spoken[2].text,/Gira a la derecha/);
  const before=c.cancellations();update(130,false);assert.ok(c.cancellations()>before);
  assert.equal(c.spoken.length,3);
  c.element('repeatdirection').onclick();assert.equal(c.spoken.length,4);
  c.guidance.update(null,'safe',null,false,scale);
  assert.equal(c.element('repeatdirection').disabled,true);
  assert.equal(c.element('directionslist').children.length,0);
});

test('unsupported speech leaves written directions usable',()=>{
  const c=controller(false);
  assert.equal(c.element('voice').disabled,true);
  c.guidance.update(data,'safe',{distance:90},true,scale);
  assert.match(c.element('directiontext').textContent,/derecha/);
  assert.equal(c.element('directiondistance').textContent,'En 30 m');
  assert.equal(c.spoken.length,0);
});
