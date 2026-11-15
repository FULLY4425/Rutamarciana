/* Offline software 3D renderer. Elevations come exclusively from the active DEM. */
let terrain = null, triangles = [], projected = [], flat = false;
let yaw = -0.35, tilt = 0.85, zoom = 1, exaggeration = 3;
let overviewCache=null, overviewTerrain=null, overviewKey='';
let running = false, progress = 0, lastFrame = 0, activeData = null;
const byId = id => document.getElementById(id);
function elevation(pt) {
  if (!terrain) return 0;
  const ri = terrain.rows.findIndex(r => r >= pt[0]), ci = terrain.cols.findIndex(c => c >= pt[1]);
  const r = Math.max(0, ri), c = Math.max(0, ci), r0 = Math.max(0,r-1), c0 = Math.max(0,c-1);
  const t = (pt[0]-terrain.rows[r0])/(terrain.rows[r]-terrain.rows[r0] || 1);
  const u = (pt[1]-terrain.cols[c0])/(terrain.cols[c]-terrain.cols[c0] || 1);
  const z = terrain.elevations;
  if ([z[r0][c0],z[r0][c],z[r][c0],z[r][c]].some(v=>v===null)) return null;
  return (z[r0][c0]*(1-u)+z[r0][c]*u)*(1-t)+(z[r][c0]*(1-u)+z[r][c]*u)*t;
}
function project(pt, z = elevation(pt)) {
  const width = (state.grid.cols-1)*state.cellXM, height=(state.grid.rows-1)*state.cellM;
  const span = Math.max(width,height), scale=680/span*zoom;
  const x=(pt[1]*state.cellXM-width/2), y=(pt[0]*state.cellM-height/2);
  const rx=x*Math.cos(yaw)-y*Math.sin(yaw), ry=x*Math.sin(yaw)+y*Math.cos(yaw);
  return [500+rx*scale,510+(ry*Math.cos(tilt)-((z ?? terrain?.base ?? 0)-(terrain?.base ?? 0))*exaggeration*Math.sin(tilt))*scale,ry];
}
function line(points,color,width,dash=[]) {
  if (!points?.length) return;
  ctx.beginPath(); points.forEach((p,i)=>{const q=flat?g2c(p):project(p);i?ctx.lineTo(q[0],q[1]):ctx.moveTo(q[0],q[1]);});
  ctx.strokeStyle=color;ctx.lineWidth=width;ctx.setLineDash(dash);ctx.stroke();ctx.setLineDash([]);
}
function marker(pt,label,color) {
  const [x,y]=flat?g2c(pt):project(pt);
  ctx.strokeStyle=color;ctx.lineWidth=2;ctx.beginPath();ctx.moveTo(x,y);ctx.lineTo(x,y-24);ctx.stroke();
  ctx.fillStyle=color;ctx.beginPath();ctx.arc(x,y-27,7,0,Math.PI*2);ctx.fill();tag(label,x+12,y-23,color);
}
function trackingPath() {
  const r=state.data?.[byId('trackroute').value];
  if (!r) return [];
  return [...(r.outbound?.points || r.points || []),...(r.return?.points || [])];
}
function samplePath(path,fraction) {
  const lengths=[0];for(let i=1;i<path.length;i++) lengths.push(lengths[i-1]+meters(path[i-1],path[i]));
  const total=lengths.at(-1)||0,target=total*fraction;
  let i=lengths.findIndex(v=>v>=target);if(i<1)i=1;if(i>=path.length)i=path.length-1;
  const a=path[i-1], b=path[i], t=(target-lengths[i-1])/(lengths[i]-lengths[i-1]||1);
  return {point:[a[0]+(b[0]-a[0])*t,a[1]+(b[1]-a[1])*t],distance:target,total,index:i};
}
function drawTerrain() {
  if (activeData !== state.data) { activeData=state.data;running=false;progress=0;byId('progress').value=0;byId('play').textContent='Iniciar'; }
  if(flat) draw2d();
  else if (!window.SurfaceView?.isOpen()) {
    const cacheKey=[yaw,tilt,zoom,exaggeration,byId('terrainpalette').value].join(':');
    if(overviewCache && overviewTerrain===terrain && overviewKey===cacheKey){ctx.drawImage(overviewCache,0,0);}
    else {
    ctx.fillStyle='#0d1215';ctx.fillRect(0,0,1000,1000);triangles=[];
    if(terrain) {
      projected=terrain.rows.map((r,i)=>terrain.cols.map((c,j)=>project([r,c],terrain.elevations[i][j])));
      for(let i=0;i<terrain.rows.length-1;i++) for(let j=0;j<terrain.cols.length-1;j++) {
        for(const ids of [[[i,j],[i+1,j],[i,j+1]],[[i+1,j],[i+1,j+1],[i,j+1]]]) {
          if(ids.some(([r,c])=>terrain.elevations[r][c]===null))continue;
          const pts=ids.map(([r,c])=>projected[r][c]);
          const zs=ids.map(([r,c])=>terrain.elevations[r][c]);
          const shade=Math.max(0.25,Math.min(1,(0.65+(zs[1]-zs[0])/35-(zs[2]-zs[0])/50)));
          triangles.push({pts,ids,depth:pts.reduce((s,p)=>s+p[2],0),shade});
        }
      }
      triangles.sort((a,b)=>a.depth-b.depth);
      for(const t of triangles) {
        ctx.beginPath(); t.pts.forEach((p,i)=>i?ctx.lineTo(p[0],p[1]):ctx.moveTo(p[0],p[1]));ctx.closePath();
        ctx.fillStyle=window.ScienceView?.triangleColor(terrain,t.ids,byId('terrainpalette').value) || `rgb(${Math.round(183*t.shade)},${Math.round(115*t.shade)},${Math.round(78*t.shade)})`;ctx.fill();ctx.strokeStyle=ctx.fillStyle;ctx.lineWidth=0.6;ctx.stroke();
      }
      // Sparse contour grid makes the physical terrain readable without invented imagery.
      for(let i=0;i<terrain.rows.length;i+=8) line(terrain.cols.map(c=>[terrain.rows[i],c]),'#efd2b320',0.8);
    } else {ctx.fillStyle='#b4a79a';ctx.font='20px monospace';ctx.fillText('Esperando el DEM del proyecto…',280,500);}
    if(terrain){
      if(!overviewCache){overviewCache=document.createElement('canvas');overviewCache.width=1000;overviewCache.height=1000;}
      overviewCache.getContext('2d').drawImage(cvs,0,0);overviewTerrain=terrain;overviewKey=cacheKey;
    }
    }
    for(const [name,color] of [['fast','#ff985f'],['safe','#e7d08b']]) {
      const r=state.data?.[name];if(!r)continue;
      line(r.points,'#151819',8);line(r.points,color,4,name==='fast'?[8,7]:[]);
      line(r.return?.points,'#65cfc3',3,[6,7]);
    }
    if(state.start)marker(state.start,'A · SALIDA','#83dfad');
    if(state.end)marker(state.end,'B · DESTINO','#ff8d8d');
    state.stops.forEach((p,i)=>marker(p,`S${i+1}`,'#68d8d0'));
  }
  const path=trackingPath();
  if(path.length>1) {
    const sample=samplePath(path,progress);
    window.ScienceView?.update({terrain,data:state.data,name:byId('trackroute').value,path,sample,elevation});
    line([...path.slice(0,sample.index),sample.point],'#ffffff',4);
    marker(sample.point,'EVA · SIMULACIÓN','#ffffff');
    const lookDistance=sample.distance<sample.total?Math.min(sample.total,sample.distance+8):Math.max(0,sample.distance-8);
    window.SurfaceView?.update({terrain,path,sample,ahead:samplePath(path,lookDistance/(sample.total||1))});
    window.MissionGuidance?.update(state.data,byId('trackroute').value,sample,running,{x:state.cellXM,y:state.cellM});
    byId('trackinfo').textContent=`${(progress*100).toFixed(0)}% · ${(sample.distance/1000).toFixed(2)} / ${(sample.total/1000).toFixed(2)} km · fila ${sample.point[0].toFixed(0)}, col ${sample.point[1].toFixed(0)}`;
  } else {
    window.ScienceView?.update({terrain,data:state.data,name:byId('trackroute').value,path,sample:null,elevation});
    window.SurfaceView?.update({terrain,path,sample:null});
    window.MissionGuidance?.update(state.data,byId('trackroute').value,null,false,{x:state.cellXM,y:state.cellM});
    byId('trackinfo').textContent='Calcula una ruta para explorar el recorrido.';
  }
}
function updateBasemap(){
  bg.src=byId('orthophoto').checked?'/reference-image':apiURL('/api/terrain-image?layer='+byId('terrainpalette').value);
}
byId('terrainpalette').onchange=()=>{updateBasemap();draw();};
window.addEventListener('terrain-photo',event=>{
  updateBasemap();
  if(event.detail){flat=true;document.querySelector('.zoomctl').hidden=false;byId('viewmode').textContent='Vista 3D';}
  draw();
});
window.addEventListener('atlas-open',()=>{running=false;window.MissionGuidance?.stop();byId('play').textContent='Continuar';});
window.addEventListener('surface-close',()=>draw());
window.addEventListener('surface-step',event=>{
  const path=trackingPath();
  if(path.length<2)return;
  const total=samplePath(path,progress).total;
  if(!total)return;
  window.MissionGuidance?.stop();running=false;
  progress=Math.max(0,Math.min(1,progress+event.detail/total));
  byId('progress').value=Math.round(progress*1000);byId('play').textContent='Continuar';draw();
});
window.drawTerrain=drawTerrain;
let pointer=null, moved=false;
cvs.style.touchAction='none';
cvs.addEventListener('pointerdown',ev=>{pointer={x:ev.clientX,y:ev.clientY,startX:ev.clientX,startY:ev.clientY};moved=false;cvs.setPointerCapture(ev.pointerId);});
cvs.addEventListener('pointermove',ev=>{
  if(!pointer)return;
  if(Math.hypot(ev.clientX-pointer.startX,ev.clientY-pointer.startY)>5)moved=true;
  if(flat){
    if(moved && state.view.k>1){const rect=cvs.getBoundingClientRect();state.view.ox+=(ev.clientX-pointer.x)/rect.width*cvs.width;state.view.oy+=(ev.clientY-pointer.y)/rect.height*cvs.height;clampView();draw();}
    pointer.x=ev.clientX;pointer.y=ev.clientY;return;
  }
  if(moved){yaw+=(ev.clientX-pointer.x)*0.006;tilt=Math.max(0.15,Math.min(1.35,tilt+(ev.clientY-pointer.y)*0.004));draw();}
  pointer.x=ev.clientX;pointer.y=ev.clientY;
});
cvs.addEventListener('pointercancel',()=>{pointer=null;});
cvs.addEventListener('pointerup',ev=>{
  if(!pointer)return;pointer=null;if(moved)return;
  const rect=cvs.getBoundingClientRect(), x=(ev.clientX-rect.left)/rect.width*1000,y=(ev.clientY-rect.top)/rect.height*1000;
  if(flat){selectPoint(c2g(ev.clientX,ev.clientY));return;}
  // Hit-test frontmost visible triangle, interpolate original grid coordinates.
  for(let k=triangles.length-1;k>=0;k--){const t=triangles[k], [a,b,c]=t.pts;
    const den=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1]);if(Math.abs(den)<1e-8)continue;
    const u=((b[1]-c[1])*(x-c[0])+(c[0]-b[0])*(y-c[1]))/den;
    const v=((c[1]-a[1])*(x-c[0])+(a[0]-c[0])*(y-c[1]))/den,w=1-u-v;
    if(u>=0&&v>=0&&w>=0){const ids=t.ids;selectPoint([Math.round(u*terrain.rows[ids[0][0]]+v*terrain.rows[ids[1][0]]+w*terrain.rows[ids[2][0]]),Math.round(u*terrain.cols[ids[0][1]]+v*terrain.cols[ids[1][1]]+w*terrain.cols[ids[2][1]])]);break;}
  }
});
cvs.addEventListener('wheel',ev=>{if(flat){ev.preventDefault();zoomAt(ev.clientX,ev.clientY,ev.deltaY<0?1.25:1/1.25);return;}ev.preventDefault();zoom=Math.max(0.5,Math.min(2.5,zoom*Math.exp(-ev.deltaY*0.001)));draw();},{passive:false});
byId('viewmode').onclick=()=>{flat=!flat;document.querySelector('.zoomctl').hidden=!flat;clampView();byId('viewmode').textContent=flat?'Vista 3D':'Vista 2D';draw();};
byId('camerareset').onclick=()=>{yaw=-0.35;tilt=0.85;zoom=1;resetView();};
byId('relief').oninput=ev=>{exaggeration=Number(ev.target.value);document.querySelector('.meta').textContent=`${state.grid.rows} × ${state.grid.cols} · RELIEVE ×${exaggeration} · ARRASTRA PARA GIRAR`;draw();};
byId('progress').oninput=ev=>{window.MissionGuidance?.stop();running=false;byId('play').textContent='Continuar';progress=Number(ev.target.value)/1000;draw();};
byId('trackroute').onchange=()=>{running=false;progress=0;byId('progress').value=0;byId('play').textContent='Iniciar';draw();};
byId('play').onclick=()=>{if(trackingPath().length<2)return;if(progress>=1)progress=0;running=!running;byId('play').textContent=running?'Pausar':'Continuar';draw();};
byId('export').onclick=()=>{
  if(!state.data){hint.textContent='Calcula una misión antes de exportar.';return;}
  const blob=new Blob([JSON.stringify({simulation:true,terrain_site:terrain?.site,...state.data},null,2)],{type:'application/json'}),url=URL.createObjectURL(blob);
  const a=document.createElement('a');a.href=url;a.download='ruta-marciana-mision.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
};
function animate(t){if(running){progress=Math.min(1,progress+Math.min((t-lastFrame)/1000,0.1)/60);byId('progress').value=Math.round(progress*1000);draw();if(progress>=1){running=false;byId('play').textContent='Repetir';}}lastFrame=t;requestAnimationFrame(animate);}
requestAnimationFrame(animate);
function terrainSourceLabel(data){
 if(data.site==='synth')return '● DEMO · TERRENO SINTÉTICO';
 if(data.site==='mola-global')return '● MARTE GLOBAL · MOLA ~3.7 KM';
 if(data.site==='jez_dtm_1m')return `● JEZERO · HiRISE ${data.cell_m.toFixed(0)} M`;
 return `● DEM · ${data.site}`;
}
function updateRegionLabels(){
 const global=terrain?.site==='mola-global',synthetic=terrain?.site==='synth';
 const name=global?`Región ${terrain.spatial.center_latitude_deg.toFixed(2)}°, ${terrain.spatial.center_longitude_deg.toFixed(2)}°`:synthetic?'Demostración sintética':terrain?.site==='jez_dtm_1m'?'Jezero · HiRISE':terrain?.site||'Terreno local';
 byId('active-region-name').textContent=name;
 byId('region-scope').textContent=global?'MOLA global: resolución nativa ~3.7 km en el ecuador, con celdas interpoladas de 5 km en esta ventana de 400 km. No permite evaluar una caminata EVA, rocas, arena ni obstáculos pequeños. Las rutas y la voz son un ensayo regional.':synthetic?'Terreno sintético de demostración: esta ventana no representa un lugar real de Marte.':'Terreno local de mayor detalle. Las rutas son simulaciones de escenario; el DEM no detecta rocas ni arena.';
 byId('region-scope').dataset.coarse=String(global);
}
let regionSwitch=0;
async function switchRegion(region){
 const request=++regionSwitch;terrainLoad++;state.revision++;
 running=false;window.MissionGuidance?.stop();byId('compute').disabled=true;byId('terraindetail').disabled=true;
 hint.textContent='Cargando la región seleccionada…';
 try{
  const [terrainResponse,metaResponse]=await Promise.all([
   fetch(apiURL('/api/terrain?detail='+byId('terraindetail').value,region)),fetch(apiURL('/api/routes',region))]);
  if(!terrainResponse.ok || !metaResponse.ok)throw Error('No se pudo cargar la región. Se conserva el mapa anterior.');
  const [loaded,meta]=await Promise.all([terrainResponse.json(),metaResponse.json()]);
  if(request!==regionSwitch)return;
  terrainLoad++;terrain=loaded;terrain.base=Math.min(...terrain.elevations.flat().filter(z=>z!==null));
  state.region=region;state.grid=loaded.grid;state.cellM=loaded.cell_m;state.cellXM=loaded.cell_x_m;
  state.start=null;state.end=null;state.stops=[];state.nStops=0;state.data=null;
  byId('nstops').value='0';byId('orthophoto').checked=false;byId('orthophoto').disabled=true;
  state.view={k:1,ox:0,oy:0};yaw=-.35;tilt=.85;zoom=1;progress=0;overviewTerrain=null;
  if(meta.routes?.safe && meta.routes?.fast){
   state.data={...meta,safe:meta.routes.safe,fast:meta.routes.fast};
   state.start=state.data.safe.points[0];state.end=state.data.safe.points.at(-1);
  }
  byId('source').textContent=terrainSourceLabel(loaded);
  document.querySelector('.meta').textContent=`${state.grid.rows} × ${state.grid.cols} · RELIEVE ×${exaggeration} · ARRASTRA PARA GIRAR`;
  mockflag.hidden=true;hint.textContent=nextHint();updateRegionLabels();updateBasemap();renderCards();draw();
  window.history?.replaceState(null,'','?region='+encodeURIComponent(region));
 }catch(error){if(request===regionSwitch)hint.textContent=error.message;}
 finally{if(request===regionSwitch){byId('compute').disabled=false;byId('terraindetail').disabled=false;}}
}
window.addEventListener('mission-region',event=>switchRegion(event.detail.region));
let terrainLoad=0;
async function loadTerrain(){
 const request=++terrainLoad;
 try{
  const response=await fetch(apiURL('/api/terrain?detail='+byId('terraindetail').value));if(!response.ok)throw Error('No se pudo cargar el DEM. Revisa el servidor.');
  const loaded=await response.json();if(request!==terrainLoad)return;terrain=loaded;
  terrain.base=Math.min(...terrain.elevations.flat().filter(z=>z!==null));state.grid=terrain.grid;state.cellM=terrain.cell_m;state.cellXM=terrain.cell_x_m;
  byId('source').textContent=terrainSourceLabel(terrain);
  document.querySelector('.meta').textContent=`${state.grid.rows} × ${state.grid.cols} · RELIEVE ×${exaggeration} · ARRASTRA PARA GIRAR`;
  updateRegionLabels();updateBasemap();
 }catch(error){if(request!==terrainLoad)return;byId('source').textContent=terrain?'● DETALLE ANTERIOR CONSERVADO':'● SIN DEM';hint.textContent=error.message;}
 draw();
}
byId('terraindetail').onchange=loadTerrain;
loadTerrain();
