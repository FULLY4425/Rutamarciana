// Optional real-browser regression. Start Python and an isolated Chromium CDP first.
const fs=require('node:fs'),assert=require('node:assert/strict');
const base=process.env.RUTA_TEST_BASE||'http://127.0.0.1:8094',cdp=process.env.RUTA_CDP_URL||'http://127.0.0.1:9227';
const pause=ms=>new Promise(r=>setTimeout(r,ms));
(async()=>{
 const target=await (await fetch(cdp+'/json/new?about:blank',{method:'PUT'})).json(),socket=new WebSocket(target.webSocketDebuggerUrl);
 await new Promise((resolve,reject)=>{socket.addEventListener('open',resolve,{once:true});socket.addEventListener('error',reject,{once:true});});
 let id=0;const pending=new Map(),errors=[],posts=[];
 socket.addEventListener('message',event=>{const message=JSON.parse(event.data);if(message.id){const job=pending.get(message.id);pending.delete(message.id);if(message.error)job.reject(Error(JSON.stringify(message.error)));else job.resolve(message.result);}else if(message.method==='Runtime.exceptionThrown')errors.push(message.params.exceptionDetails.exception?.description||message.params.exceptionDetails.text);else if(message.method==='Network.requestWillBeSent'&&message.params.request.url.includes('/api/route?')&&message.params.request.method==='POST')posts.push(JSON.parse(message.params.request.postData));});
 const send=(method,params={})=>new Promise((resolve,reject)=>{const key=++id;pending.set(key,{resolve,reject});socket.send(JSON.stringify({id:key,method,params}));});
 const evaluate=async(expression)=>{const r=await send('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});if(r.exceptionDetails)throw Error(r.exceptionDetails.exception?.description||r.exceptionDetails.text);return r.result.value;};
 const wait=async(expression)=>{for(let i=0;i<100;i++){if(await evaluate(expression))return;await pause(150);}throw Error('Timed out: '+expression);};
 const tap=async(x,y)=>{await send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[{x,y,id:1}]});await send('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]});await pause(120);};
 const button=async name=>{await evaluate(`document.getElementById(${JSON.stringify(name)}).scrollIntoView({block:'center',behavior:'instant'})`);await pause(150);const r=await evaluate(`(()=>{const e=document.getElementById(${JSON.stringify(name)}),r=e.getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2,width:r.width,height:r.height,hit:document.elementFromPoint(r.x+r.width/2,r.y+r.height/2)?.closest('button')?.id};})()`);assert.equal(r.hit,name,'Button must receive touch: '+name);assert.ok(r.height>=44,'Touch target too small: '+name);await tap(r.x,r.y);await pause(450);};
 const mapPoint=async point=>{await evaluate(`document.getElementById('map').scrollIntoView({block:'center',behavior:'instant'})`);await pause(150);const r=await evaluate(`(()=>{const c=document.getElementById('map'),r=c.getBoundingClientRect();return {x:r.x+(${point[1]}/(state.grid.cols-1)*c.width*state.view.k+state.view.ox)/c.width*r.width,y:r.y+(${point[0]}/(state.grid.rows-1)*c.height*state.view.k+state.view.oy)/c.height*r.height};})()`);await tap(r.x,r.y);};
 await Promise.all([send('Page.enable'),send('Runtime.enable'),send('Network.enable')]);
 await send('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});await send('Emulation.setTouchEmulationEnabled',{enabled:true,maxTouchPoints:5});
 await send('Emulation.setEmulatedMedia',{features:[{name:'prefers-reduced-motion',value:'reduce'}]});
 await send('Page.addScriptToEvaluateOnNewDocument',{source:'localStorage.clear();'});
 await send('Page.navigate',{url:base+'/?region=glen-torridon'});await wait("typeof terrain!=='undefined'&&terrain?.site==='glen-torridon'&&window.MapEditor&&document.getElementById('chemistrystatus').textContent.includes('878')");
 assert.equal(await evaluate('flat'),true,'Phone starts in 2D');
 await button('editstart');await mapPoint([30,30]);assert.deepEqual(await evaluate('state.start'),[30,30]);
 await button('editend');await mapPoint([30,40]);assert.deepEqual(await evaluate('state.end'),[30,40]);
 await evaluate("document.getElementById('maxslope').value='40';document.getElementById('maxslope').oninput()");
 await button('mapcompute');await wait('Boolean(state.data?.fast&&state.data?.safe)');
 const midpoint=await evaluate('state.data.fast.points[Math.floor(state.data.fast.points.length/2)]');
 await button('editstop');await mapPoint(midpoint);assert.equal(await evaluate('state.stops.length'),1);
 await button('editzone');assert.equal(await evaluate("document.getElementById('mapzones').open"),true);
 await button('obstacleplace');await mapPoint([40,70]);assert.equal(await evaluate('ScienceMission.exclusions().length'),1);
 await button('mapcompute');await wait('Boolean(state.data?.fast)');assert.equal(posts.at(-1).environment.blocked_zones[0].kind,'manual_obstacle');assert.equal(posts.at(-1).science_waypoints.length,1);
 await evaluate("document.getElementById('map').scrollIntoView({block:'center',behavior:'instant'})");
 const before=await evaluate('JSON.stringify([state.start,state.end,state.stops])'),r=await evaluate("(()=>{const r=document.getElementById('map').getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2};})()");
 await send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[{x:r.x-30,y:r.y,id:1},{x:r.x+30,y:r.y,id:2}]});await send('Input.dispatchTouchEvent',{type:'touchMove',touchPoints:[{x:r.x-60,y:r.y,id:1},{x:r.x+60,y:r.y,id:2}]});await send('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]});
 assert.ok(await evaluate('state.view.k>1'));assert.equal(await evaluate('JSON.stringify([state.start,state.end,state.stops])'),before,'Pinch must not add points');
 for(const [width,height] of [[320,568],[390,844],[844,390],[768,1024],[1280,800]]){
  await send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:width<1000});await pause(180);
  const panels=await evaluate('Array.from(document.querySelectorAll(".panel")).map(e=>({overflow:getComputedStyle(e).overflowY,maxHeight:getComputedStyle(e).maxHeight,content:e.scrollHeight,height:e.clientHeight}))');
  for(const panel of panels){assert.equal(panel.overflow,'visible',`Nested panel scroll at ${width}`);assert.equal(panel.maxHeight,'none');assert.ok(panel.content<=panel.height+2,`Clipped options at ${width}`);}
  const frame=await evaluate('({bottom:document.getElementById("map-section").getBoundingClientRect().bottom,forms:document.getElementById("mission-controls").getBoundingClientRect().top})');assert.ok(frame.forms>=frame.bottom,`Forms stretch the map frame at ${width}`);
  const metrics=await evaluate('({viewport:window.innerWidth,scroll:document.documentElement.scrollWidth})');assert.ok(metrics.scroll<=metrics.viewport+1,`Horizontal overflow at ${width}: ${JSON.stringify(metrics)}`);
  for(const name of ['editstart','editend','editzone','obstacleplace','compute','mission-controls','mission-guidance','route-results'])assert.equal(await evaluate(`(()=>{const e=document.getElementById('${name}');return getComputedStyle(e).display!=='none'&&e.getBoundingClientRect().width>0;})()`),true,`${name} unavailable at ${width}`);
 }
 await send('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});
 await evaluate("document.getElementById('map').scrollIntoView({block:'center',behavior:'instant'})");await pause(150);
 const image=await send('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});fs.writeFileSync('/tmp/ruta-mobile-map.png',Buffer.from(image.data,'base64'));
 await button('planetopen');assert.equal(await evaluate("getComputedStyle(document.getElementById('planetview')).position"),'fixed');
 await evaluate("document.getElementById('planetload').scrollIntoView({block:'center',behavior:'instant'})");assert.equal(await evaluate("(()=>{const r=document.getElementById('planetload').getBoundingClientRect();return r.top>=0&&r.bottom<=innerHeight;})()"),true,'Atlas selection can be reached');
 await button('planetclose');
 if(process.env.RUTA_TEST_WATCH==='1'){
  await wait('Boolean(window.ojoDeMarte)');
  const shared=await evaluate(`(async()=>{const p=ojoDeMarte.plan(),r=await fetch('/api/psion/missions',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(p)});if(!r.ok)throw Error(JSON.stringify(await r.json()));return r.json();})()`);
  assert.equal(shared.region,'glen-torridon');assert.equal(shared.stops.length,3);shared.stops[0].name='Salida del reloj';
  await evaluate('ojoDeMarte.setPlan('+JSON.stringify(shared)+')');assert.equal(await evaluate('state.stops.length'),1);assert.equal(await evaluate('ScienceMission.exclusions().length'),1);assert.equal(await evaluate('FieldPoints.namedPoints()[0].name'),'Salida del reloj');
  for(const mode of ['terrain','follow','surface','globe']){
   const size=await evaluate(`(async()=>{const s=await ojoDeMarte.view3d({mode:'${mode}',snapshot:true,p:.5});if(!s.image.startsWith('data:image/png;base64,'))throw Error('Missing image');return s.image.length;})()`);
   assert.ok(size>2000,mode+' snapshot missing');
  }
  console.log('PASS: watch mission sharing, round trip, preserved exclusions and terrain/follow/surface/globe image snapshots.');
 }
 assert.deepEqual(errors,[],'Browser runtime exceptions');console.log('PASS: real touch A/B, science stop, obstacle, Python route request, pinch, five viewport widths, atlas and no runtime errors.');console.log('Screenshot: /tmp/ruta-mobile-map.png');
 socket.close();await fetch(cdp+'/json/close/'+target.id);
})().catch(error=>{console.error(error);process.exit(1);});
