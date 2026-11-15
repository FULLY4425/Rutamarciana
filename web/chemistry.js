/* Quantitative PDS observations, target markers only: no spatial interpolation. */
(function(root){
 const OXIDES=['SiO2','TiO2','Al2O3','FeOT','MgO','CaO','Na2O','K2O','MnO'];
 function summarize(observations,oxide){
  const grouped=new Map();
  for(const obs of observations){const value=obs.oxides?.[oxide]?.wt_percent;if(!Number.isFinite(value)||value<0)continue;
   if(!grouped.has(obs.target_id))grouped.set(obs.target_id,{target_id:obs.target_id,lat:obs.lat,lon:obs.lon,values:[]});grouped.get(obs.target_id).values.push(value);
  }
  return [...grouped.values()].map(g=>{g.values.sort((a,b)=>a-b);const n=g.values.length;return {...g,value:(g.values[Math.floor((n-1)/2)]+g.values[Math.floor(n/2)])/2,count:n};});
 }
 if(typeof module==='object'&&module.exports){module.exports={summarize};return;}
 const el=id=>document.getElementById(id);let region=null,terrain=null,documentData=null,version=0,selected=null,measure=0,markers=[];
 function refresh(){
  markers=summarize(documentData?.observations||[],el('chemistryoxide').value||'CaO');
  const values=markers.map(m=>m.value),low=Math.min(...values),high=Math.max(...values);
  el('chemistrylegend').textContent=markers.length?`${el('chemistryoxide').value}: mediana por objetivo ${low.toFixed(2)}–${high.toFixed(2)} wt% · ${markers.length} objetivos con valor. Rango de esta tabla, no de todo Marte.`:'Sin valores químicos en esta región.';
  for(const m of markers)m.t=(m.value-low)/(high-low||1);details();draw();
 }
 function details(){
  const pane=el('chemistrydetail');pane.replaceChildren();if(!selected)return;
  const observations=(documentData?.observations||[]).filter(o=>o.target_id===selected);
  const title=document.createElement('p');title.textContent=observations.length?`${observations.length} observaciones del objetivo · ChemCam LIBS`:'Este objetivo no tiene observaciones cuantitativas en las tablas integradas. No se infieren valores.';pane.appendChild(title);
  if(!observations.length)return;
  const selector=document.createElement('select');selector.setAttribute('aria-label','Disparo químico observado');
  observations.forEach((o,i)=>{const option=document.createElement('option');option.value=String(i);option.textContent=`${o.category} · punto ${o.observation_point} · sol ${o.sol}`;selector.appendChild(option);});
  measure=Math.min(measure,observations.length-1);selector.value=String(measure);selector.onchange=()=>{measure=Number(selector.value);details();};pane.appendChild(selector);
  const observation=observations[measure],facts=document.createElement('dl');
  for(const oxide of OXIDES){const value=observation.oxides[oxide],name=document.createElement('dt'),text=document.createElement('dd');name.textContent=oxide;text.textContent=Number.isFinite(value.wt_percent)?`${value.wt_percent} wt% · RMSEP ${value.rmsep_wt_percent??'no disponible'}`:'Sin valor';facts.appendChild(name);facts.appendChild(text);}pane.appendChild(facts);
  const info=document.createElement('p');info.textContent=`Secuencia ${observation.sequence}. Coordenada del objetivo, no del disparo individual. RMSEP es error de predicción, no intervalo de confianza. No es composición medida hoy.`;pane.appendChild(info);
  const source=document.createElement('a');source.href='https://pds-geosciences.wustl.edu/msl/urn-nasa-pds-msl_gt_diagenesis_supplement/';source.target='_blank';source.rel='noopener noreferrer';source.textContent='Tabla y etiquetas PDS originales';pane.appendChild(source);
 }
 function update(next){terrain=next;if(!next||region===state.region)return;region=state.region;const request=++version;documentData=null;selected=null;markers=[];el('chemistrydetail').replaceChildren();el('chemistrystatus').textContent='Cargando tabla PDS de la región…';
  fetch(apiURL('/api/chemistry')).then(r=>{if(!r.ok)throw Error();return r.json();}).then(data=>{if(request!==version)return;documentData=data;el('chemistrystatus').textContent=data.observations.length?`${data.observations.length} observaciones · MSL/ChemCam · datos históricos. Combinables con óptica MRO/HiRISE y topografía.`:'Sin química incorporada aquí. Abre Glen Torridon para combinar óptica, topografía y química real.';refresh();}).catch(()=>{if(request===version)el('chemistrystatus').textContent='No se pudo cargar química; no se muestran valores inventados.';});
 }
 function paint(ctx,project){if(!terrain||!el('chemistryvisible').checked||!root.FieldPoints)return;
  for(const m of markers){const point=root.FieldPoints.geoToGrid(m.lat,m.lon,terrain);if(!point)continue;const p=project(point);ctx.strokeStyle=`rgb(${Math.round(65+175*m.t)},${Math.round(205-100*m.t)},225)`;ctx.lineWidth=m.target_id===selected?4:2;ctx.beginPath();ctx.arc(p[0],p[1],m.target_id===selected?10:5,0,Math.PI*2);ctx.stroke();}
 }
 el('chemistrycombine').onclick=()=>{if(!documentData?.observations?.length||el('orthophoto').disabled){el('chemistrystatus').textContent='Abre Glen Torridon y espera a que se compruebe la ortofoto compatible.';return;}el('orthophoto').checked=true;el('chemistryvisible').checked=true;el('orthophoto').onchange();draw();};
 el('chemistryoxide').onchange=refresh;el('chemistryvisible').onchange=()=>draw();
 root.ChemistryLayer={update,paint,select(id){selected=id;measure=0;details();},metadata(){return {topographic:{available:Boolean(terrain),site:terrain?.site,source:terrain?.provenance?.source_url,cell_m:terrain?.cell_m,cell_x_m:terrain?.cell_x_m},optical:{available:!el('orthophoto').disabled,visible:el('orthophoto').checked,reference_metadata_url:apiURL('/reference-metadata'),reference_image_url:apiURL('/reference-image')},chemical:{available:Boolean(documentData?.observations?.length),instrument:documentData?.instrument,units:documentData?.units,source:documentData?.sources||[],spatial_assignment:documentData?.spatial_assignment,observation_period:documentData?.observation_period,selected_oxide:el('chemistryoxide').value,marker_statistic:'median_of_observed_points_per_target',interpolation:false}};}};
})(typeof window==='object'?window:globalThis);
