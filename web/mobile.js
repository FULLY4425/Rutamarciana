/* Discoverable route editing and navigation, shared by touch and desktop. */
(function(root){
 const el=id=>document.getElementById(id);let role='auto',last='';
 const buttons={start:'editstart',end:'editend',stop:'editstop',pan:'editpan'};
 function showMap(target='map'){root.MarsAtlas?.close();root.SurfaceView?.close?.();root.WorkspaceSections?.reveal(el(target));el(target).scrollIntoView?.({block:target==='map'?'center':'start',behavior:root.matchMedia?.('(prefers-reduced-motion: reduce)').matches?'auto':'smooth'});}
 function choose(next){
  root.dispatchEvent(new CustomEvent('pick-mode',{detail:'route-'+next}));role=next;
  showMap();update();
 }
 function update(){
  for(const [name,id]of Object.entries(buttons))el(id).setAttribute('aria-pressed',String(role===name||(role==='auto'&&((name==='start'&&!state.start)||(name==='end'&&state.start&&!state.end)))));
  const instruction=role==='pan'?'Mover mapa: arrastra con un dedo. Usa dos dedos para acercar o desplazar.':role==='external'?'Toca el mapa para colocar el punto o área que acabas de elegir.':role==='start'?'Toca el mapa para colocar o cambiar la salida A.':role==='end'?'Toca el mapa para colocar o cambiar el destino B.':role==='stop'?'Toca el mapa para añadir una parada científica.':!state.start?'1. Toca el mapa para marcar la salida A.':!state.end?'2. Toca el mapa para marcar el destino B.':`A y B listos · ${state.stops.length} paradas. Puedes añadir una parada o calcular.`;
  if(instruction!==last){last=instruction;el('mapeditstatus').textContent=instruction;}
  el('mapcompute').disabled=el('compute').disabled||!state.start||!state.end||state.stops.length!==state.nStops;
  el('mapdimension').textContent=root.MissionMapControls?.isFlat()?'Ver en 3D':'Ver en 2D';
 }
 function pick(point){
  if(role==='pan')return true;
  if(!['start','end','stop'].includes(role))return false;
  const points=[state.start,state.end,...state.stops].filter(Boolean);
  if(points.some(p=>p[0]===point[0]&&p[1]===point[1])){el('mapeditstatus').textContent='Ese punto ya está en el recorrido. Elige otra celda.';return true;}
  if(role==='start')state.start=point;
  else if(role==='end')state.end=point;
  else{
   if(!state.start||!state.end){el('mapeditstatus').textContent='Marca primero la salida A y el destino B.';return true;}
   if(state.stops.length>=18){el('mapeditstatus').textContent='La misión admite hasta 18 paradas científicas.';return true;}
   state.stops.push(point);state.nStops=state.stops.length;el('nstops').value=String(state.nStops);
  }
  role=role==='start'&&!state.end?'end':'auto';last='';missionChanged();update();return true;
 }
 for(const [name,id]of Object.entries(buttons))el(id).onclick=()=>choose(name);
 el('editzone').onclick=()=>{el('mapzones').open=true;showMap('mapzones');};
 el('mapdimension').onclick=()=>{el('viewmode').onclick();update();};
 el('mapcompute').onclick=async()=>{if(el('compute').disabled)return;await compute();showMap('route-results');};
 el('mapzoomin').onclick=()=>root.MissionMapControls?.zoom(1.3);el('mapzoomout').onclick=()=>root.MissionMapControls?.zoom(1/1.3);
 root.addEventListener('pick-mode',event=>{
  if(String(event.detail||'').startsWith('route-'))return;
  role=event.detail?'external':'auto';last='';
  if(event.detail)showMap();
  update();
 });
 root.addEventListener('region-ready',()=>{role='auto';last='';update();});
 root.MapEditor={update,handlePick:pick,isPan:()=>role==='pan',finishPick(){role='auto';last='';update();}};
 const options=el('mapoptions');if(root.matchMedia?.('(max-width:700px), (max-width:1000px) and (pointer:coarse)').matches)options.open=false;
 update();
})(window);
