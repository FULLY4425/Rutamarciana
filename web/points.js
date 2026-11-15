/* Region-scoped science catalog, named personal points and ordered mission editing. */
(function(root){
 'use strict';
 const deg=r=>r*180/Math.PI,rad=d=>d*Math.PI/180;
 function gridToGeo(point,terrain){
  const s=terrain?.spatial;if(!s)return null;
  if(s.degree_step)return {lat:s.north_deg-point[0]*s.degree_step,lon:s.west_deg+point[1]*s.degree_step};
  if(s.crs==='ESRI:103885'){
   const [left,bottom,right,top]=s.bounds,R=3396190;
   return {lat:deg((top-(point[0]+.5)*(top-bottom)/terrain.grid.rows)/R),lon:deg((left+(point[1]+.5)*(right-left)/terrain.grid.cols)/R)};
  }
  if(s.crs==='MARS:SPHERICAL_AEQD'){
   const x=(point[1]-(terrain.grid.cols-1)/2)*terrain.cell_x_m,y=((terrain.grid.rows-1)/2-point[0])*terrain.cell_m;
   const rho=Math.hypot(x,y),a=rho/s.radius_m,lat0=rad(s.center_latitude_deg),lon0=rad(s.center_longitude_deg);
   if(!rho)return {lat:s.center_latitude_deg,lon:s.center_longitude_deg};
   return {lat:deg(Math.asin(Math.max(-1,Math.min(1,Math.cos(a)*Math.sin(lat0)+y*Math.sin(a)*Math.cos(lat0)/rho)))),lon:((deg(lon0+Math.atan2(x*Math.sin(a),rho*Math.cos(lat0)*Math.cos(a)-y*Math.sin(lat0)*Math.sin(a)))+180)%360+360)%360-180};
  }return null;
 }
 function geoToGrid(lat,lon,terrain){
  if(!Number.isFinite(lat)||!Number.isFinite(lon)||Math.abs(lat)>90)return null;
  const s=terrain?.spatial;if(!s)return null;let row,col;
  if(s.degree_step){row=(s.north_deg-lat)/s.degree_step;col=(lon-s.west_deg)/s.degree_step;}
  else if(s.crs==='ESRI:103885'){
   const [left,bottom,right,top]=s.bounds,R=3396190;const x=rad((lon%360+360)%360)*R,y=rad(lat)*R;
   row=(top-y)/(top-bottom)*terrain.grid.rows-.5;col=(x-left)/(right-left)*terrain.grid.cols-.5;
  }else if(s.crs==='MARS:SPHERICAL_AEQD'){
   const p=rad(lat),p0=rad(s.center_latitude_deg),d=rad(lon-s.center_longitude_deg);
   const a=Math.acos(Math.max(-1,Math.min(1,Math.sin(p0)*Math.sin(p)+Math.cos(p0)*Math.cos(p)*Math.cos(d))));
   if(a>.15)return null;const k=a>1e-8?a/Math.sin(a):1;
   col=(terrain.grid.cols-1)/2+s.radius_m*k*Math.cos(p)*Math.sin(d)/terrain.cell_x_m;
   row=(terrain.grid.rows-1)/2-s.radius_m*k*(Math.cos(p0)*Math.sin(p)-Math.sin(p0)*Math.cos(p)*Math.cos(d))/terrain.cell_m;
  }else return null;
  const point=[Math.round(row),Math.round(col)];
  return point.every(Number.isFinite)&&point[0]>=0&&point[0]<terrain.grid.rows&&point[1]>=0&&point[1]<terrain.grid.cols?point:null;
 }
 if(typeof module==='object'&&module.exports){module.exports={gridToGeo,geoToGrid};return;}
 const el=id=>document.getElementById(id),key=p=>p.join(',');
 let terrain=null,scope='',custom=[],targets=[],selected=null,placing=false,revision=0,listSignature='';
 const labels=new Map();
 function mission(){return [state.start,...state.stops,state.end].filter(Boolean);}
 function namedPoints(){return mission().map((point,index)=>{
  const entry=labels.get(key(point));return {point,name:entry?.name||(['Salida',...state.stops.map((_,i)=>`Ciencia S${i+1}`),'Destino'][index]||'Punto'),source:entry?.source||'user_map',coordinates:gridToGeo(point,terrain),...(entry?.observation?{observation:entry.observation}:{})};
 });}
 function reflect(){
  const points=namedPoints(),signature=JSON.stringify(points);if(signature===listSignature)return;listSignature=signature;
  el('missionpoints').replaceChildren();
  points.forEach((point,index)=>{const item=document.createElement('li'),text=document.createElement('span'),remove=document.createElement('button');text.textContent=`${index+1}. ${point.name}`;remove.textContent='Quitar';remove.type='button';remove.onclick=()=>{
    const next=mission().filter((_,i)=>i!==index);state.start=next[0]||null;state.end=next.length>1?next.at(-1):null;state.stops=next.slice(1,-1);state.nStops=state.stops.length;el('nstops').value=String(state.nStops);missionChanged();
  };item.appendChild(text);item.appendChild(remove);el('missionpoints').appendChild(item);});
  el('missionpointcount').textContent=`${points.length} / 20 puntos · salida, ciencia y destino`;
 }
 function add(point,name,source='custom',observation=null){
  if(mission().length>=20){el('pointstatus').textContent='El máximo es 20 puntos por misión.';return;}
  if(mission().some(p=>key(p)===key(point))){el('pointstatus').textContent='Ese punto ya está en el recorrido.';return;}
  labels.set(key(point),{name,source,observation});
  if(!state.start)state.start=point;else if(!state.end)state.end=point;else state.stops.push(point);
  state.nStops=state.stops.length;el('nstops').value=String(state.nStops);missionChanged();reflect();
 }
 function save(){try{root.localStorage.setItem(scope,JSON.stringify(custom));}catch{el('pointstatus').textContent='El navegador no permite guardar; los puntos permanecerán durante esta sesión.';}}
 function customList(){el('custompoints').replaceChildren();for(const point of custom){
  const item=document.createElement('li'),title=document.createElement('span'),addButton=document.createElement('button'),remove=document.createElement('button');title.textContent=point.name;
  addButton.textContent='Añadir';addButton.type='button';addButton.onclick=()=>add(point.point,point.name);
  remove.textContent='Eliminar';remove.type='button';remove.onclick=()=>{custom=custom.filter(p=>p.id!==point.id);save();customList();draw();};
  item.appendChild(title);item.appendChild(addButton);item.appendChild(remove);el('custompoints').appendChild(item);
 }}
 function create(point){
  if(custom.length>=100){el('pointstatus').textContent='Máximo 100 puntos guardados en esta región.';return;}
  const name=el('pointname').value.trim().slice(0,80)||`Punto ${custom.length+1}`;
  const entry={id:root.crypto?.randomUUID?.()||`${Date.now()}-${Math.random()}`,point,name,coordinates:gridToGeo(point,terrain)};
  custom.push(entry);labels.set(key(point),{name,source:'custom'});save();customList();el('pointstatus').textContent=`${name} guardado en este navegador. Pulsa Añadir para usarlo en la misión.`;draw();
 }
 function filtered(){const filter=el('targetfilter').value,search=el('targetsearch').value.toLowerCase();return targets.filter(t=>(filter==='all'||filter==='veins'&&t.veins||filter==='nodules'&&t.nodules)&&`${t.name} ${t.formation} ${t.member} ${t.sol}`.toLowerCase().includes(search));}
 function targetOptions(){el('targetselect').replaceChildren();const placeholder=document.createElement('option');placeholder.value='';placeholder.textContent='Selecciona un objetivo científico';el('targetselect').appendChild(placeholder);
  for(const target of filtered()){const option=document.createElement('option');option.value=target.id;option.textContent=`${target.name} · sol ${target.sol}`;el('targetselect').appendChild(option);}
  el('targetcount').textContent=`${filtered().length} / ${targets.length} registros en esta región`;selected=null;el('targetadd').disabled=true;draw();
 }
 function update(next){
  terrain=next;if(!terrain)return;
  const nextScope='ruta-marciana:points:'+JSON.stringify({region:state.region,site:terrain.site,grid:terrain.grid,spatial:terrain.spatial});
  if(nextScope!==scope){scope=nextScope;placing=false;labels.clear();custom=[];targets=[];selected=null;const request=++revision;
   try{const saved=JSON.parse(root.localStorage.getItem(scope)||'[]');if(Array.isArray(saved))custom=saved.filter(p=>p&&typeof p.name==='string'&&p.name.length<=80&&Array.isArray(p.point)&&p.point.length===2&&p.point.every(Number.isInteger)&&p.point[0]>=0&&p.point[0]<terrain.grid.rows&&p.point[1]>=0&&p.point[1]<terrain.grid.cols).slice(0,100);}catch{}
   custom.forEach(p=>labels.set(key(p.point),{name:p.name,source:'custom'}));customList();
   fetch(apiURL('/api/science-targets')).then(r=>{if(!r.ok)throw Error();return r.json();}).then(data=>{if(request!==revision)return;targets=data.targets;targetOptions();el('targetcontext').textContent=targets.length?'Glen Torridon / Gale · NASA PDS ChemCam. Los objetivos se ajustan a la celda más cercana para planificar.':'Los 718 registros corresponden a Glen Torridon. Abre esa región para consultarlos y mezclarlos con puntos propios.';}).catch(()=>{if(request===revision)el('targetcontext').textContent='No se pudo cargar el catálogo científico.';});
   el('pointcoordinates').disabled=!gridToGeo([0,0],terrain);listSignature='';
  }reflect();
 }
 function paint(context,project){
  for(const target of filtered()){const point=geoToGrid(target.lat,target.lon,terrain);if(!point)continue;const [x,y]=project(point);context.fillStyle=selected?.id===target.id?'#fff3bc':'#72cfc4';context.beginPath();context.arc(x,y,selected?.id===target.id?6:2.5,0,Math.PI*2);context.fill();}
  for(const point of custom){const [x,y]=project(point.point);context.strokeStyle='#e8b8ff';context.lineWidth=2;context.strokeRect(x-4,y-4,8,8);}
 }
 el('pointplace').onclick=()=>{placing=!placing;root.dispatchEvent(new CustomEvent('pick-mode',{detail:placing?'custom':null}));el('pointstatus').textContent=placing?'Pulsa el mapa 2D o 3D para guardar el punto con el nombre indicado.':'Marcado cancelado.';};
 root.addEventListener('pick-mode',event=>{if(event.detail!=='custom')placing=false;});
 el('pointcoordinates').onclick=()=>{if(!terrain)return;const point=geoToGrid(Number(el('pointlat').value),Number(el('pointlon').value),terrain);if(!point){el('pointstatus').textContent='Las coordenadas quedan fuera del recorte actual.';return;}create(point);};
 for(const id of ['targetfilter','targetsearch'])el(id).oninput=targetOptions;
 el('targetselect').onchange=event=>{selected=targets.find(t=>t.id===event.target.value)||null;el('targetadd').disabled=!selected;root.ChemistryLayer?.select(selected?.id);el('targetfacts').textContent=selected?`${selected.name} · sol ${selected.sol} · ${selected.formation} / ${selected.member} · vetas: ${selected.veins?'sí':'no'} · nódulos: ${selected.nodules?'sí':'no'} · ${selected.lat.toFixed(6)}°, ${selected.lon.toFixed(6)}° E`:'Selecciona un objetivo.';draw();};
 el('targetadd').onclick=()=>{if(!selected||!terrain)return;const point=geoToGrid(selected.lat,selected.lon,terrain);if(point)add(point,selected.name,'NASA/PDS ChemCam',selected);else el('pointstatus').textContent='El objetivo está fuera de la rejilla actual.';};
 el('openglen').onclick=()=>{root.MarsAtlas?.close();root.dispatchEvent(new CustomEvent('mission-region',{detail:{region:'glen-torridon'}}));};
 function setMissionNames(names){
  const points=mission();if(!Array.isArray(names)||names.length!==points.length||names.some(n=>typeof n!=='string'||!n.length||n.length>160))throw Error('Nombres de misión no válidos.');
  points.forEach((point,i)=>labels.set(key(point),{name:names[i],source:'watch_mission'}));listSignature='';reflect();
 }
 root.FieldPoints={update,paint,namedPoints,setMissionNames,gridToGeo,geoToGrid,handlePick(point){if(!placing)return false;placing=false;create(point);return true;}};
})(typeof window==='object'?window:globalThis);
