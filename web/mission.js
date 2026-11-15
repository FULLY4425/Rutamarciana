/* Science intent, field notes and manual exclusions share the current region only. */
(function(root){
 if(typeof module==='object'&&module.exports){module.exports={};return;}
 const el=id=>document.getElementById(id);let terrain=null,scope='',zones=[],notes=[],placing=false,pointSignature='';
 const controls=['missiongoal','missionhypothesis','missionequipment','missionmass','missionmobility'];
 function request(){return {science_goal:el('missiongoal').value||'',hypothesis:el('missionhypothesis').value||'',equipment:el('missionequipment').value||'',equipment_mass_kg:Number(el('missionmass').value||'0'),mobility_factor:Number(el('missionmobility').value||'1')};}
 function persist(){if(!scope)return;try{root.localStorage.setItem(scope,JSON.stringify({mission:request(),zones,notes}));}catch{el('notestatus').textContent='Almacenamiento no disponible; los cambios duran esta sesión.';}}
 function changed(){persist();missionChanged({preserveHome:true});}
 function list(){
  el('obstaclelist').replaceChildren();zones.forEach((z,i)=>{const item=document.createElement('li'),remove=document.createElement('button');item.textContent=`Área ${i+1}: fila ${z.center[0]}, col ${z.center[1]} · ${z.radius_m} m`;remove.textContent='Quitar';remove.onclick=()=>{zones.splice(i,1);list();changed();};item.appendChild(remove);el('obstaclelist').appendChild(item);});
  el('notelist').replaceChildren();notes.forEach((n,i)=>{const item=document.createElement('li'),remove=document.createElement('button');item.textContent=`${n.point_name} · ${n.text}`;remove.textContent='Eliminar';remove.onclick=()=>{notes.splice(i,1);persist();list();};item.appendChild(remove);el('notelist').appendChild(item);});
 }
 function update(value){terrain=value;if(!terrain)return;const key='ruta-marciana:mission:'+state.region+':'+terrain.site+':'+JSON.stringify({grid:terrain.grid,spatial:terrain.spatial});
  if(scope!==key){scope=key;zones=[];notes=[];placing=false;pointSignature='';
   el('missiongoal').value=state.region==='glen-torridon'?'Comparar vetas y nódulos de Glen Torridon mediante imágenes, relieve y química ChemCam.':'';
   el('missionhypothesis').value='';el('missionequipment').value='';el('missionmass').value='0';el('missionmobility').value='1';
   try{const saved=JSON.parse(root.localStorage.getItem(scope)||'null');if(saved&&typeof saved==='object'){
    const m=saved.mission||{};for(const [id,key,limit]of [['missiongoal','science_goal',600],['missionhypothesis','hypothesis',600],['missionequipment','equipment',300]])if(typeof m[key]==='string')el(id).value=m[key].slice(0,limit);
    if(Number.isFinite(m.equipment_mass_kg)&&m.equipment_mass_kg>=0&&m.equipment_mass_kg<=100)el('missionmass').value=String(m.equipment_mass_kg);
    if(Number.isFinite(m.mobility_factor)&&m.mobility_factor>=.5&&m.mobility_factor<=1)el('missionmobility').value=String(m.mobility_factor);
    if(Array.isArray(saved.zones))zones=saved.zones.filter(z=>z&&Array.isArray(z.center)&&z.center.length===2&&z.center.every(Number.isInteger)&&z.center[0]>=0&&z.center[1]>=0&&z.center[0]<terrain.grid.rows&&z.center[1]<terrain.grid.cols&&Number.isFinite(z.radius_m)&&z.radius_m>=1&&z.radius_m<=10000).slice(0,7);
    if(Array.isArray(saved.notes))notes=saved.notes.filter(n=>n&&typeof n.text==='string'&&n.text.length<=600&&typeof n.point_name==='string'&&n.point_name.length<=160).slice(0,100);
   }}catch{}
   el('missionperiod').textContent=state.region==='glen-torridon'?'Campaña histórica Glen Torridon: consulta sol y fechas de cada tabla PDS; no condiciones en tiempo real.':'Contexto orbital estático. No se conoce meteorología actual de la ruta.';list();
  }
  const points=root.FieldPoints?.namedPoints()||[],signature=JSON.stringify(points.map(p=>[p.point,p.name]));if(signature!==pointSignature){pointSignature=signature;el('notepoint').replaceChildren();points.forEach((p,i)=>{const option=document.createElement('option');option.value=String(i);option.textContent=p.name;el('notepoint').appendChild(option);});}
 }
 for(const id of controls)el(id).onchange=changed;
 el('obstacleplace').onclick=()=>{placing=!placing;root.dispatchEvent(new CustomEvent('pick-mode',{detail:placing?'obstacle':null}));el('obstaclestatus').textContent=placing?'Pulsa sobre el mapa para excluir una zona.':'Marcado cancelado.';};
 root.addEventListener('pick-mode',event=>{if(event.detail!=='obstacle')placing=false;});
 el('noteadd').onclick=()=>{const points=root.FieldPoints?.namedPoints()||[],point=points[Number(el('notepoint').value)],text=el('notetext').value.trim().slice(0,600);if(!point||!text){el('notestatus').textContent='Elige una parada y escribe una observación.';return;}if(notes.length>=100){el('notestatus').textContent='Máximo 100 observaciones por región.';return;}notes.push({point:point.point,point_name:point.name,text,created_at:new Date().toISOString(),source:'user_observation_unverified'});el('notetext').value='';persist();list();el('notestatus').textContent='Observación guardada; se incluirá al exportar.';};
 function replaceExclusions(value){
  if(!terrain||!Array.isArray(value)||value.length>7||value.some(z=>!z||!Array.isArray(z.center)||z.center.length!==2||z.center.some((v,i)=>!Number.isFinite(v)||v<0||v>=(i?terrain.grid.cols:terrain.grid.rows))||!Number.isFinite(z.radius_m)||z.radius_m<1||z.radius_m>Math.min(10000,Math.hypot(terrain.grid.rows*terrain.cell_m,terrain.grid.cols*terrain.cell_x_m))))throw Error('Exclusiones fuera del terreno o del límite de siete áreas.');
  zones=value.map(z=>({center:[...z.center],radius_m:z.radius_m}));list();persist();
 }
 root.ScienceMission={update,request,replaceExclusions,notes:()=>notes.map(n=>({...n})),exclusions:()=>zones.map(z=>({center:[...z.center],radius_m:z.radius_m,kind:'manual_obstacle'})),handlePick(point){if(!placing)return false;placing=false;const radius=Number(el('obstacleradius').value);if(zones.length>=7||!Number.isFinite(radius)||radius<1||radius>Math.min(10000,Math.hypot(terrain.grid.rows*terrain.cell_m,terrain.grid.cols*terrain.cell_x_m))){el('obstaclestatus').textContent='Hasta 7 zonas; radio 1–10000 m y no mayor que la diagonal del terreno.';return true;}zones.push({center:[...point],radius_m:radius});el('obstaclestatus').textContent='Exclusión manual añadida. Recalcula para evitarla.';list();changed();return true;}};
})(typeof window==='object'?window:globalThis);
