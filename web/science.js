/* Scientific presentation only: colors, provenance and sampled elevation profiles. */
(function(root){
  'use strict';
  function triangleColor(terrain,ids,palette){
    const vertices=ids.map(([r,c])=>[terrain.cols[c]*terrain.cell_x_m,terrain.elevations[r][c],terrain.rows[r]*terrain.cell_m]);
    const a=vertices[1].map((v,i)=>v-vertices[0][i]), b=vertices[2].map((v,i)=>v-vertices[0][i]);
    let normal=[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
    if(normal[1]<0)normal=normal.map(v=>-v);
    const magnitude=Math.hypot(...normal)||1;
    const light=0.42+0.58*Math.max(0,(-0.6*normal[0]+0.8*normal[1]-0.3*normal[2])/(magnitude*Math.hypot(0.6,0.8,0.3)));
    let color=[190,128,87];
    if(palette==='elevation'){
      const stats=terrain.statistics||{},lo=stats.elevation_min_m??0,hi=stats.elevation_max_m??1;
      const height=vertices.reduce((sum,p)=>sum+p[1],0)/3;
      const t=Math.max(0,Math.min(1,(height-lo)/(hi-lo||1)));
      color=[55+185*t,103+104*t,111+48*t];
    }else if(palette==='slope'){
      const values=ids.map(([r,c])=>terrain.slopes?.[r]?.[c]);
      if(values.some(v=>v==null))return 'rgb(70,70,70)';
      const t=Math.min(1,values.reduce((sum,v)=>sum+v,0)/3/25);
      color=[80+160*t,175-105*t,126-65*t];
    }
    return `rgb(${color.map(v=>Math.round(v*light)).join(',')})`;
  }
  function buildProfile(path,terrain,elevation){
    let distance=0;
    const points=path.map((point,i)=>{
      if(i)distance+=Math.hypot((point[0]-path[i-1][0])*terrain.cell_m,(point[1]-path[i-1][1])*terrain.cell_x_m);
      return {distance,height:elevation(point)};
    });
    const valid=points.filter(p=>p.height!==null && Number.isFinite(p.height));
    if(!valid.length)return {points:[],total:distance,low:0,high:0};
    return {points,total:distance,low:Math.min(...valid.map(p=>p.height)),high:Math.max(...valid.map(p=>p.height))};
  }
  const api={triangleColor,buildProfile};
  if(typeof module==='object' && module.exports){module.exports=api;return;}
  const el=id=>document.getElementById(id);
  let currentTerrain=null,reference=null,profileData=null,profileName=null,profile=null,referenceRegion=null,referenceRequest=0;
  function photoStatus(){
    const aligned=Boolean(root.SurfaceGeometry?.alignedTexture(currentTerrain,reference));
    el('orthophoto').disabled=!aligned;
    el('photostatus').textContent=aligned?'CRS, límites y ventana de origen coinciden. Fotografía orbital; precisión absoluta no verificada.':'Ortofoto disponible como referencia: no se superpone a un terreno de otra ventana o sintético.';
  }
  function update({terrain,data,name,path,sample,elevation}){
    if(!terrain)return;
    if(currentTerrain!==terrain){
      currentTerrain=terrain;
      if(referenceRegion!==state.region){
       referenceRegion=state.region;reference=null;const request=++referenceRequest;
       fetch(apiURL('/reference-metadata')).then(r=>r.ok?r.json():null).then(value=>{if(request!==referenceRequest)return;reference=value;photoStatus();}).catch(()=>{});
      }
      photoStatus();
      const stats=terrain.statistics,p=terrain.provenance;
      el('terrainfacts').textContent=terrain.site==='synth'?'Terreno sintético de demostración.':`${p?(terrain.site==='mola-global'?'MOLA / NASA PDS · ':'HiRISE / USGS · '):''}${terrain.grid.rows} × ${terrain.grid.cols} celdas · ${terrain.cell_x_m.toFixed(2)} × ${terrain.cell_m.toFixed(2)} m por celda. ${stats?.elevation_min_m!=null?`Elevación ${stats.elevation_min_m.toFixed(0)} a ${stats.elevation_max_m.toFixed(0)} m, referencia vertical del DEM.`:''}`;
      if(p)el('terrainfacts').textContent+=` Fuente nativa ${p.source_resolution_m} m; malla visual ${terrain.rows.length} × ${terrain.cols.length}.`;
      profileData=null;profileName=null;
    }
    const palette=el('terrainpalette').value;
    el('layerlegend').textContent=palette==='slope'?'Verde → rojo: 0° → 25° o más. Pendiente del DEM, sin detección de rocas.':palette==='elevation'?'Azul verdoso → arena: menor → mayor elevación. Colores ilustrativos.':'Iluminación calculada a partir de las alturas. El relieve exagerado solo afecta la vista aérea.';
    if(profileData!==data || profileName!==name){
      profileData=data;profileName=name;profile=buildProfile(path,terrain,elevation);
      let down=false;
      const d=profile.points.map(point=>{
        if(point.height===null || !Number.isFinite(point.height)){down=false;return '';}
        const x=8+284*point.distance/(profile.total||1),y=85-70*(point.height-profile.low)/(profile.high-profile.low||1);
        const command=down?'L':'M';down=true;return `${command}${x.toFixed(1)},${y.toFixed(1)}`;
      }).join(' ');
      el('elevationline').setAttribute('d',d);
      el('elevationinfo').textContent=profile.points.length?`${profile.low.toFixed(0)} a ${profile.high.toFixed(0)} m · ${(profile.total/1000).toFixed(2)} km · ida y regreso`:'Calcula una ruta para ver su perfil.';
    }
    const height=sample?elevation(sample.point):null;
    el('elevationposition').setAttribute('visibility',height===null || !profile?.points.length?'hidden':'visible');
    if(height!==null && profile?.points.length){
      el('elevationposition').setAttribute('cx',String(8+284*sample.distance/(profile.total||1)));
      el('elevationposition').setAttribute('cy',String(85-70*(height-profile.low)/(profile.high-profile.low||1)));
    }
  }
  el('orthophoto').onchange=()=>root.dispatchEvent(new CustomEvent('terrain-photo',{detail:el('orthophoto').checked}));
  fetch('/api/science-sources').then(r=>{if(!r.ok)throw Error();return r.json();}).then(catalog=>{
    for(const source of catalog.sources){
      const item=document.createElement('li'),link=document.createElement('a'),note=document.createElement('p');
      link.href=source.url;link.target='_blank';link.rel='noopener';link.textContent=source.title+' ↗';
      note.textContent=`${source.region} · ${source.status==='integrated'?'DATOS INTEGRADOS':source.status==='excluded'?'NO APLICABLE A MARTE':'REFERENCIA'}. ${source.description}`;
      item.appendChild(link);item.appendChild(note);el('sciencesources').appendChild(item);
    }
    el('sourcesstatus').textContent='MOLA, HiRISE y ChemCam incorporados; cobertura y referencias diferenciadas por región.';
  }).catch(()=>{el('sourcesstatus').textContent='No se pudo cargar el catálogo. La planificación sigue disponible.';});
  root.ScienceView={...api,update};
})(typeof window==='object'?window:globalThis);
