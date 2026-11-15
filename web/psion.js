/* Compatibility adapter for the supplied Mars OS relay. Uses this project's state and planner. */
(function(root){
 'use strict';
 const el=id=>document.getElementById(id),WEATHER={clear:1,dust:.8,storm:.6};
 let importedNames=[];
 function requireGlen(){if(terrain?.site!=='glen-torridon')throw Error('Mars OS sin conexión requiere Glen Torridon; la guía del celular funciona en los demás recortes.');}
 async function mars(){if(terrain?.site!=='glen-torridon'){await switchRegion('glen-torridon');requireGlen();}return true;}
 function plan(){
  requireGlen();if([state.start,...state.stops,state.end].filter(Boolean).length>10)throw Error('El reloj admite hasta 10 puntos; usa la guía del celular para rutas más largas.');const named=root.FieldPoints.namedPoints();
  return {region:'glen-torridon',stops:[state.start,...state.stops,state.end].filter(Boolean).map((p,i)=>({name:importedNames[i]||named[i]?.name||`Punto ${i+1}`,x:p[1],y:p[0],...root.FieldPoints.gridToGeo(p,terrain),custom:true})),
   maxSlopeDegrees:Number(el('maxslope').value),weather:el('weather').value,routed:Boolean(state.data),status:hint.textContent,
   environment:root.FieldOperations.environment()};
 }
 function weather(){
  requireGlen();const env=root.FieldOperations.environment();return {weather:env.weather,label:el('weather').selectedOptions?.[0]?.textContent||env.weather,sourceType:'synthetic',
   model:'user_hypothetical_training_scenario',notice:'Escenario educativo; no es meteorología observada.',visibility:'Escenario simulado',speedKmH:2*WEATHER[env.weather],
   restrictions:env.blocked_zones.map(z=>({name:z.kind,...root.FieldPoints.gridToGeo(z.center,terrain),radiusMeters:z.radius_m})),
   scenarios:[{key:'clear',label:'Despejado'},{key:'dust',label:'Polvo hipotético'},{key:'storm',label:'Tormenta hipotética'}]};
 }
 async function setPlan(payload){
  if(!payload||!Array.isArray(payload.stops)||payload.stops.length>10)throw Error('Mars OS admite 0–10 puntos.');
  await mars();
  const limit=payload.maxSlopeDegrees??20,kind=payload.environment?.weather??payload.weather??payload.weatherKey??'clear';
  if(!Number.isFinite(limit)||limit<5||limit>40||!Object.hasOwn(WEATHER,kind))throw Error('Pendiente o escenario no válido.');
  let targets=null;const names=[];const points=[];
  for(let i=0;i<payload.stops.length;i++){
   let p=payload.stops[i];if(typeof p==='string'){targets??=await fetch('/api/science-targets?region=glen-torridon').then(r=>r.json());p=targets.targets.find(t=>t.name===p);}
   if(!p||typeof p.name!=='string'||p.name.length>27)throw Error('Objetivo desconocido o nombre demasiado largo.');
   const point=root.FieldPoints.geoToGrid(p.lat,p.lon,terrain);if(!point)throw Error('Punto fuera de Glen Torridon.');
   if(points.some(q=>q[0]===point[0]&&q[1]===point[1]))throw Error('Puntos duplicados.');points.push(point);names.push(p.name);
  }
  let zones=payload.environment?.blocked_zones??[];
  if(!Array.isArray(zones)||zones.length>8)throw Error('Exclusiones no válidas.');
  zones=zones.filter(z=>z.kind!=='hypothetical_dust');
  root.ScienceMission.replaceExclusions(zones);
  root.MissionPlayback.pause();state.start=points[0]||null;state.end=points.length>1?points.at(-1):null;state.stops=points.slice(1,-1);state.nStops=state.stops.length;state.home=null;
  el('nstops').value=String(state.nStops);el('maxslope').value=String(limit);el('maxslope').oninput();el('weather').value=kind;
  missionChanged();importedNames=names;root.FieldPoints.setMissionNames(names);
  if(points.length>=2)await compute();else draw();return plan();
 }
 async function setWeather(key){requireGlen();if(!Object.hasOwn(WEATHER,key))throw Error('Escenario desconocido.');el('weather').value=key;missionChanged();if(state.start&&state.end)await compute();else draw();return weather();}
 async function view3d(options={}){
  requireGlen();root.MissionPlayback.pause();const mode=options.mode||'terrain';
  if(!['terrain','follow','surface','globe'].includes(mode))throw Error('Vista desconocida.');
  if(mode==='globe')return {image:root.MarsAtlas.snapshot(),mode};
  root.MarsAtlas.close();root.SurfaceView.close();
  if(mode==='follow'||mode==='surface'){
   if(!state.data)throw Error('Calcula una ruta antes de seguirla.');
   progress=Math.max(0,Math.min(1,Number(options.p)||0));el('progress').value=String(Math.round(progress*1000));
  }
  if(mode==='surface'){draw();return {image:root.SurfaceView.snapshot(),mode};}
  flat=false;el('viewmode').textContent='Vista 2D';document.querySelector('.zoomctl').hidden=true;
  if(Number.isFinite(options.yaw))yaw=options.yaw;
  if(Number.isFinite(options.pitch))tilt=Math.max(.15,Math.min(1.35,options.pitch));
  if(Number.isFinite(options.zoom))zoom=Math.max(.5,Math.min(10,options.zoom));
  panX=0;panY=0;el('followcamera').checked=mode==='follow';draw();
  return {image:cvs.toDataURL('image/png'),mode};
 }
 root.ojoDeMarte={version:1,ready:()=>Boolean(terrain),mars,terrain(){requireGlen();const s=terrain.spatial;return {width:terrain.grid.cols,height:terrain.grid.rows,west:s.west_deg,north:s.north_deg,degreeStep:s.degree_step,cellMeters:terrain.cell_m,cellXMeters:terrain.cell_x_m};},plan,setPlan,weather,setWeather,view3d};
 root.addEventListener('mission-edited',()=>{importedNames=[];});
 el('psionsend').onclick=async()=>{try{const p=plan();if(p.stops.length<2)throw Error('Marca salida y destino en Glen Torridon.');const r=await fetch('/api/psion/missions',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(p)}),data=await r.json();if(!r.ok)throw Error(data.error);el('psionstatus').textContent=`Código público: ${data.code} · caduca en 24 h o al reiniciar el servidor. En reloj: MISIONES → Recibir de la web.`;}catch(e){el('psionstatus').textContent=e.message;}};
 el('psionreceive').onclick=async()=>{try{const code=el('psioncode').value.trim().toUpperCase();if(!/^[A-Z2-9]{4}$/.test(code))throw Error('Introduce el código de cuatro caracteres.');const r=await fetch('/api/psion/missions/'+code),p=await r.json();if(!r.ok)throw Error(p.error);await setPlan(p);el('psionstatus').textContent='Misión recibida y recalculada con Python; el cálculo local del reloj puede diferir.';}catch(e){el('psionstatus').textContent=e.message;}};
})(window);
