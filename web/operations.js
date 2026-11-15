/* Replanning and hypothetical dust. Positions are simulated, never GPS. */
(function(root){
 function distanceToRoute(point,path,scale){
  let nearest=Infinity;
  for(let i=1;i<path.length;i++){
   const ax=(path[i-1][1]-point[1])*scale.x,ay=(path[i-1][0]-point[0])*scale.y;
   const bx=(path[i][1]-point[1])*scale.x,by=(path[i][0]-point[0])*scale.y,dx=bx-ax,dy=by-ay;
   const t=Math.max(0,Math.min(1,-(ax*dx+ay*dy)/(dx*dx+dy*dy||1)));
   nearest=Math.min(nearest,Math.hypot(ax+t*dx,ay+t*dy));
  }return nearest;
 }
 function remaining(data,profile,sample){
  const route=data[profile],home=data.return_to||route.return_to||route.return?.points.at(-1)||data.start;
  const returning=sample.distance>=route.outbound.distance_m-.1;
  let accumulated=0;const stops=[];
  if(!returning)for(let i=0;i<(data.science_waypoints||[]).length;i++){
   accumulated+=route.outbound.legs[i].distance_m;
   if(accumulated>sample.distance+.1)stops.push(data.science_waypoints[i]);
  }
  return {home,end:returning?home:route.outbound.points.at(-1),stops};
 }
 if(typeof module==='object'&&module.exports){module.exports={distanceToRoute,remaining};return;}
 const el=id=>document.getElementById(id);
 let latest=null,region=null,placing=false,offRoute=null,saved=null,operation=0;
 function message(text,blocked=false){el('replanstatus').textContent=text;el('replanstatus').dataset.blocked=String(blocked);}
 function environment(){
  const weather=el('weather').value||'clear',terrain=latest?.terrain;
  return {weather,blocked_zones:[...(root.ScienceMission?.exclusions()||[]),...(weather==='storm'&&terrain?[{center:[Math.floor(terrain.grid.rows*.55),Math.floor(terrain.grid.cols*.55)],radius_m:Math.max(30,terrain.cell_m*3),kind:'hypothetical_dust'}]:[])]};
 }
 async function replan(origin){
  if(!latest?.data&&!saved){message('Calcula una ruta antes de simular un desvío.');return;}
  const request=++operation,resume=Boolean(latest?.running);
  const context=latest?.data&&latest.sample?remaining(latest.data,latest.profile,latest.sample):saved;
  if(!context){message('No hay un recorrido para recalcular.');return;}
  saved=context;
  const point=(origin||offRoute||latest.sample?.point)?.map(Math.round);
  if(!point){message('No hay posición simulada disponible.');return;}
  offRoute=point;root.MissionPlayback.pause();
  if(point[0]===context.end[0]&&point[1]===context.end[1]){message('La posición coincide con el destino; el tramo está completado.');return;}
  state.start=point;state.end=context.end;state.stops=context.stops.filter(p=>p[0]!==point[0]||p[1]!==point[1]);state.nStops=state.stops.length;state.home=context.home;
  el('nstops').value=String(state.nStops);missionChanged({preserveHome:true});message('Recalculando desde la posición simulada…');
  const ok=await compute();if(request!==operation)return;
  if(ok){offRoute=null;message('Ruta recalculada. Se conserva el regreso al origen H; presupuesto del nuevo tramo, sin descontar tiempo consumido.');if(resume)root.MissionPlayback.play();}
  else message('Sin paso con las restricciones actuales. Simulación pausada. Cambia el escenario, la pendiente o la posición y pulsa Reintentar.',true);
 }
 function update(value){
  latest=value;if(!value.terrain)return;
  const key=state.region+':'+value.terrain.site;
  if(key!==region){region=key;operation++;saved=null;offRoute=null;placing=false;el('weather').value='clear';message('Sin posición real ni meteorología en vivo.');}
  const active=Boolean(value.sample&&value.data);el('deviate').disabled=!active;el('setposition').disabled=!active&&!saved;el('retryroute').disabled=!active&&!saved;
  const veil=document.querySelector('.weather-veil');if(veil)veil.style.opacity=el('weather').value==='storm'?'.23':el('weather').value==='dust'?'.12':'0';
 }
 function paint(ctx,project){
  const terrain=latest?.terrain;if(!terrain)return;
  for(const zone of environment().blocked_zones){
   ctx.beginPath();let down=false;
   for(let i=0;i<=64;i++){const a=i/64*Math.PI*2,p=[zone.center[0]+Math.sin(a)*zone.radius_m/terrain.cell_m,zone.center[1]+Math.cos(a)*zone.radius_m/terrain.cell_x_m];
    if(p[0]<0||p[0]>=terrain.grid.rows||p[1]<0||p[1]>=terrain.grid.cols){down=false;continue;}
    const q=project(p);down?ctx.lineTo(q[0],q[1]):ctx.moveTo(q[0],q[1]);down=true;
   }ctx.fillStyle='#e59c412b';ctx.strokeStyle='#efb366';ctx.lineWidth=2;ctx.fill();ctx.setLineDash([7,6]);ctx.stroke();ctx.setLineDash([]);
  }
  if(state.home){const p=project(state.home);ctx.fillStyle="#b6efc7";ctx.font="bold 13px sans-serif";ctx.fillText("H",p[0]+9,p[1]+16);}
  if(offRoute){const p=project(offRoute);ctx.strokeStyle='#ffb184';ctx.lineWidth=3;ctx.strokeRect(p[0]-6,p[1]-6,12,12);ctx.fillStyle='#ffd4ae';ctx.font='12px sans-serif';ctx.fillText('POSICIÓN SIMULADA',p[0]+10,p[1]-8);}
 }
 el('weather').onchange=()=>{if(latest?.data&&latest.sample)replan();else{message('Escenario hipotético aplicado al próximo cálculo.');draw();}};
 el('maxslope').oninput=()=>{el('slopevalue').textContent=el('maxslope').value+'°';};
 el('maxslope').onchange=()=>{if(latest?.data&&latest.sample)replan();};
 el('deviate').onclick=()=>{
  if(!latest?.sample)return;const t=latest.terrain,step=Math.max(1,Math.ceil(40/Math.min(t.cell_m,t.cell_x_m))),origin=latest.sample.point;
  for(const [dr,dc]of [[0,step],[0,-step],[step,0],[-step,0],[step,step],[-step,-step]]){
   const point=[Math.round(origin[0]+dr),Math.round(origin[1]+dc)];
   if(point[0]<0||point[1]<0||point[0]>=t.grid.rows||point[1]>=t.grid.cols)continue;
   if(distanceToRoute(point,latest.path,{x:t.cell_x_m,y:t.cell_m})>30){replan(point);return;}
  }message('No hay espacio para simular un desvío de 30 m en esta parte del recorte. Marca una posición manual.');
 };
 el('setposition').onclick=()=>{placing=!placing;root.dispatchEvent(new CustomEvent('pick-mode',{detail:placing?'position':null}));message(placing?'Pulsa el mapa para indicar la posición simulada desde la que recalcular.':'Selección cancelada.');};
 el('retryroute').onclick=()=>replan();
 root.addEventListener('mission-edited',()=>{operation++;saved=null;offRoute=null;root.MissionPlayback?.pause();});
 root.addEventListener('pick-mode',event=>{if(event.detail!=='position')placing=false;});
 root.FieldOperations={update,paint,environment,replan,handlePick(point){if(!placing)return false;placing=false;replan(point);return true;}};
})(typeof window==='object'?window:globalThis);
