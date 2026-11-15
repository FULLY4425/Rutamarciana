/* Phone GPS -> explicit local Mars training frame -> compact directions -> ESP. */
(function(root){
 'use strict';
 const rad=x=>x*Math.PI/180,R=6371008.8,MAX_AGE=15000,MAX_ACCURACY=25;
 function displacement(origin,fix){
  const dl=((fix.longitude-origin.longitude+540)%360)-180;
  return {east:R*rad(dl)*Math.cos(rad((origin.latitude+fix.latitude)/2)),north:R*rad(fix.latitude-origin.latitude)};
 }
 function trainingPoint(anchor,fix,terrain){
  const {east,north}=displacement(anchor.fix,fix),a=rad(anchor.rotation);
  return [anchor.point[0]-(-east*Math.sin(a)+north*Math.cos(a))/terrain.cell_m,
          anchor.point[1]+(east*Math.cos(a)+north*Math.sin(a))/terrain.cell_x_m];
 }
 function validFix(fix,now=Date.now()){
  return Boolean(fix&&Number.isFinite(fix.latitude)&&Math.abs(fix.latitude)<=90&&Number.isFinite(fix.longitude)&&Math.abs(fix.longitude)<=180&&Number.isFinite(fix.accuracy)&&fix.accuracy>=0&&fix.accuracy<=MAX_ACCURACY&&Number.isFinite(fix.timestamp)&&now-fix.timestamp<=MAX_AGE&&now-fix.timestamp>=-5000);
 }
 function course(path,sample,scale,rotation=0){
  for(let i=sample.index;i<path.length;i++){
   const east=(path[i][1]-sample.point[1])*scale.x,north=-(path[i][0]-sample.point[0])*scale.y;
   if(Math.hypot(east,north)<.5)continue;
   const degrees=(Math.atan2(east,north)*180/Math.PI+rotation+360)%360;
   return {degrees,cardinal:['N','NE','E','SE','S','SO','O','NO'][Math.round(degrees/45)%8]};
  }return null;
 }
 function nearest(point,path,scale,previous=0,windowM=35){
  let length=0,best=null;
  for(let i=1;i<path.length;i++){
   const a=path[i-1],b=path[i],dx=(b[1]-a[1])*scale.x,dy=(b[0]-a[0])*scale.y,segment=Math.hypot(dx,dy);
   const t=Math.max(0,Math.min(1,(((point[1]-a[1])*scale.x)*dx+((point[0]-a[0])*scale.y)*dy)/(segment*segment||1)));
   const distance=length+t*segment,projected=[a[0]+(b[0]-a[0])*t,a[1]+(b[1]-a[1])*t],offset=Math.hypot((point[1]-projected[1])*scale.x,(point[0]-projected[0])*scale.y);
   if(distance>=previous-windowM&&distance<=previous+windowM){
    const score=offset+Math.abs(distance-previous)*.03;
    if(!best||score<best.score-1e-6||Math.abs(score-best.score)<1e-6&&distance>best.distance)best={point:projected,distance,offset,index:i,score};
   }length+=segment;
  }
  return best?{...best,total:length}:null;
 }
 if(typeof module==='object'&&module.exports){module.exports={displacement,trainingPoint,validFix,nearest,course};return;}
 const el=id=>document.getElementById(id);let latest,watch=null,fix=null,anchor=null,live=null,livePoint=null,lastFix=null,traveled=0,movementWindow=35;
 let session=null,pairRevision=0,publishing=false,pending=false,lastPublish=0,alarm='',wake=null;
 const gpsOn=()=>watch!==null;
 function resetAnchor(){anchor=null;live=null;livePoint=null;lastFix=null;traveled=0;el('navanchor').disabled=!validFix(fix);}
 function stopGPS(note='GPS detenido. Vuelve a vincular para entrenar.'){
  if(watch!==null)navigator.geolocation?.clearWatch(watch);watch=null;fix=null;resetAnchor();alarm=note;root.MissionGuidance?.stop();el('navgps').disabled=false;el('navstopgps').disabled=true;el('navfix').textContent=note;draw();publish(true);
 }
 async function keepAwake(){try{wake=await navigator.wakeLock?.request('screen');}catch{}}
 el('navopen').onclick=()=>{el('navigator').hidden=false;keepAwake();draw();};
 el('navclose').onclick=()=>{el('navigator').hidden=true;wake?.release().catch(()=>{});wake=null;};
 el('navvoice').onclick=()=>el('voice').click();el('navsimulate').onclick=()=>{if(!gpsOn())el('play').click();};
 el('navgps').onclick=()=>{
  if(!latest?.data||latest.path.length<2){alarm='Calcula una ruta primero.';draw();return;}
  if(latest.terrain?.site==='mola-global'){alarm='Para entrenar a pie usa el recorte HiRISE de Jezero o Glen Torridon; MOLA es demasiado grueso.';draw();return;}
  if(!navigator.geolocation){alarm='Este navegador no ofrece GPS. Usa un celular con HTTPS.';draw();return;}
  root.MissionPlayback?.pause();fix=null;resetAnchor();alarm='Esperando permiso y señal GPS…';
  try{watch=navigator.geolocation.watchPosition(position=>{
   fix={latitude:position.coords.latitude,longitude:position.coords.longitude,accuracy:position.coords.accuracy,timestamp:position.timestamp};
   if(validFix(fix)){
    if(lastFix){const shift=displacement(lastFix,fix);movementWindow=Math.max(35,Math.min(100,Math.hypot(shift.east,shift.north)+fix.accuracy*2));}lastFix=fix;
   }
   el('navanchor').disabled=!validFix(fix);draw();
  },error=>{alarm='Sin GPS: '+error.message;fix=null;live=null;root.MissionGuidance?.stop();draw();publish(true);},{enableHighAccuracy:true,maximumAge:0,timeout:12000});}catch(error){alarm="No se pudo activar GPS: "+error.message;watch=null;draw();return;}
  el('navgps').disabled=true;el('navstopgps').disabled=false;draw();
 };
 el('navanchor').onclick=()=>{
  if(!validFix(fix)||!latest?.sample)return;
  const rotation=Number(el('navrotation').value);if(!Number.isFinite(rotation)||rotation<0||rotation>=360){alarm='La rotación debe estar entre 0 y 359 grados.';draw();return;}
  anchor={fix:{...fix},point:[...latest.sample.point],rotation};traveled=latest.sample.distance;live=null;alarm='';draw();
 };
 el('navstopgps').onclick=()=>stopGPS();
 el('navreplan').onclick=()=>{if(livePoint&&validFix(fix))root.FieldOperations?.replan(livePoint.map(Math.round));};
 function track(value){
  const changed=latest&&(latest.data!==value.data||latest.profile!==value.profile);
  // Replanning within the same terrain preserves the Earth/Mars reference frame.
  if(changed){traveled=0;movementWindow=35;live=null;if(!value.data)root.MissionGuidance?.stop();}
  latest=value;livePoint=null;live=null;
  if(!gpsOn())return value.sample;
  el('navfix').textContent=fix?`${fix.latitude.toFixed(6)}°, ${fix.longitude.toFixed(6)}° · precisión ±${Math.round(fix.accuracy)} m · edad ${Math.max(0,Math.round((Date.now()-fix.timestamp)/1000))} s`:'Esperando ubicación del celular…';
  if(!validFix(fix)){alarm=fix?'Señal antigua o precisión peor que ±25 m. Detente hasta recuperar GPS.':'Esperando una ubicación GPS válida.';return value.sample;}
  if(!anchor){alarm='Señal lista. Pulsa Vincular aquí para fijar tu posición terrestre al punto actual de la ruta.';return value.sample;}
  if(!value.terrain){alarm='Sin terreno cargado.';return value.sample;}
  const offset=displacement(anchor.fix,fix);if(Math.hypot(offset.east,offset.north)>5000){alarm='Fuera del alcance local de entrenamiento (5 km).';return value.sample;}
  livePoint=trainingPoint(anchor,fix,value.terrain);
  if(livePoint[0]<0||livePoint[1]<0||livePoint[0]>=value.terrain.grid.rows||livePoint[1]>=value.terrain.grid.cols){alarm='Tu posición vinculada queda fuera del recorte. Vuelve al área de entrenamiento.';return value.sample;}
  if(!value.data||value.path.length<2){alarm='Sin ruta calculada. Reintenta desde tu posición o vuelve al mapa.';return value.sample;}
  live=nearest(livePoint,value.path,{x:value.terrain.cell_x_m,y:value.terrain.cell_m},traveled,movementWindow);
  if(!live||live.offset>30){alarm='Te alejaste del tramo activo. Regresa a la ruta marcada antes de continuar.';live=null;return value.sample;}
  traveled=live.distance;latest.sample=live;alarm='';return live;
 }
 function payload(){
  const mode=gpsOn()?'training':'simulation';
  const good=mode==='training'?Boolean(live&&validFix(fix)):Boolean(latest?.data&&latest?.sample);
  const playing=mode==='training'?good:Boolean(latest?.running);
  const plan=root.MissionNavigation.build(latest?.data?.[latest?.profile],{x:latest?.terrain?.cell_x_m||1,y:latest?.terrain?.cell_m||1});
  const position=good?root.MissionNavigation.atPosition(plan,(live||latest.sample).distance):null,next=position?.next||position?.step;
  const direction=mode==='training'&&live&&anchor?course(latest.path,live,{x:latest.terrain.cell_x_m,y:latest.terrain.cell_m},anchor.rotation):null;
  const heading=direction?`Rumbo ${Math.round(direction.degrees)}° ${direction.cardinal}. `:'';
  const budget=latest?.data?{
   fast_out_s:latest.data.fast?.outbound?.time_s||0,fast_back_s:latest.data.fast?.return?.time_s||0,
   safe_out_s:latest.data.safe?.outbound?.time_s||0,safe_back_s:latest.data.safe?.return?.time_s||0,
   stops:latest.data.science_stops||0,dwell_s:latest.data.science_s||0}:null;
  return {mode,status:mode==='training'?good?'live':livePoint?'off_route':'waiting':playing?'simulation':'paused',kind:good?next?.kind||'straight':'wait',
   instruction:(good?(heading+(next?.text||'Sigue la ruta.')):(alarm||'Calcula una ruta en el mapa.')).slice(0,180),distance_m:position?.distance||0,remaining_m:position?.remaining||0,
   accuracy_m:fix&&Number.isFinite(fix.accuracy)?Math.min(1e6,fix.accuracy):0,fix_age_ms:fix?Math.min(1e9,Math.max(0,Date.now()-fix.timestamp)):0,budget};
 }
 function update(){
  if(!latest)return;const value=payload();
  el('navmode').textContent=value.mode==='training'?'GPS REAL → ENTRENAMIENTO MARCIANO · 1:1':'SIMULACIÓN · sin GPS';
  el('navsymbol').textContent=value.kind==='wait'?'Ⅱ':el('directionsymbol').textContent;
  el('navdistance').textContent=value.kind==='wait'?'En espera':el('directiondistance').textContent;
  el('navinstruction').textContent=value.instruction;el('navremaining').textContent=value.kind==='wait'?'':el('directionremaining').textContent;
  el('navstatus').textContent=alarm||(value.mode==='training'?'Posición actualizada desde el GPS del celular.':'La simulación no mide tus pasos.');el('navstatus').dataset.alert=String(Boolean(alarm));
  el('navreplan').disabled=!(livePoint&&validFix(fix)&&latest.terrain&&livePoint[0]>=0&&livePoint[1]>=0&&livePoint[0]<latest.terrain.grid.rows&&livePoint[1]<latest.terrain.grid.cols);
  el('navsimulate').disabled=gpsOn();el('play').disabled=gpsOn();el('progress').disabled=gpsOn();el('trackroute').disabled=gpsOn();
  if(gpsOn()&&!live)root.MissionGuidance?.stop();publish();
 }
 async function publish(force=false){
  if(!session)return;if(publishing){if(force)pending=true;return;}
  if(!force&&Date.now()-lastPublish<2000)return;lastPublish=Date.now();publishing=true;
  const current=session;
  try{
   const response=await fetch('/api/navigator/sessions/'+current.id,{method:'PUT',headers:{'Content-Type':'application/json',Authorization:'Bearer '+current.write_token},body:JSON.stringify(payload()),signal:AbortSignal.timeout(6000)});
   if(session!==current)return;
   if(response.status===404){el('navpair').disabled=false;el('navcode').textContent='';}
   if(!response.ok)throw Error(response.status===404?'El vínculo caducó; crea uno nuevo.':'El servidor rechazó la actualización.');
   el('navlink').textContent='Guía enviada al ESP · '+new Date().toLocaleTimeString();
  }catch(error){if(session===current)el('navlink').textContent='Sin enlace: '+error.message;}
  finally{publishing=false;if(pending){pending=false;publish(true);}}
 }
 el('navpair').onclick=async()=>{
  const revision=++pairRevision;el('navpair').disabled=true;
  try{
   const response=await fetch('/api/navigator/sessions',{method:'POST',signal:AbortSignal.timeout(6000)});if(!response.ok)throw Error('No se pudo crear el vínculo.');
   const created=await response.json();if(revision!==pairRevision)return;session=created;
   el('navcode').textContent=created.id+'.'+created.read_token;el('navunpair').disabled=false;publish(true);
  }catch(error){el('navlink').textContent=error.message;el('navpair').disabled=false;}
 };
 el('navunpair').onclick=()=>{
  pairRevision++;const old=session;session=null;el('navcode').textContent='';el('navpair').disabled=false;el('navunpair').disabled=true;el('navlink').textContent='Desvinculado.';
  if(old)fetch('/api/navigator/sessions/'+old.id,{method:'DELETE',headers:{Authorization:'Bearer '+old.write_token},signal:AbortSignal.timeout(6000)}).catch(()=>{});
 };
 root.addEventListener('region-ready',()=>{if(gpsOn())stopGPS('Región cambiada. Vuelve a vincular tu posición.');else{resetAnchor();alarm='';}});
 root.addEventListener('mission-edited',()=>{resetAnchor();});
 document.addEventListener('visibilitychange',()=>{if(document.hidden&&gpsOn())stopGPS('GPS pausado al ocultar la página. Mantén la guía visible para entrenar.');if(document.hidden){wake?.release().catch(()=>{});wake=null;}else if(!el('navigator').hidden)keepAwake();});
 root.addEventListener('pagehide',()=>{if(watch!==null)navigator.geolocation?.clearWatch(watch);watch=null;fix=null;live=null;root.MissionGuidance?.stop();});
 root.setInterval?.(()=>{if(gpsOn())draw();else publish();},1000);
 root.FieldNavigator={track,update,guidanceSample:sample=>gpsOn()?live:sample,isTracking:()=>Boolean(gpsOn()&&live&&validFix(fix)),paint(ctx,project){if(!livePoint)return;const p=project(livePoint);ctx.strokeStyle='#71c9ff';ctx.lineWidth=3;ctx.beginPath();ctx.arc(p[0],p[1],9,0,Math.PI*2);ctx.stroke();}};
})(typeof window==='object'?window:globalThis);
