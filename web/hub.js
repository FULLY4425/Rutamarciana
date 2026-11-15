/* Local Mars solar-time estimate and local-first field notes. */
(function(root){
 'use strict';
 const MARS_DAY=1.0274912517, MSD_EPOCH=2451549.5;
 function marsClock(date,longitude=77.4){
  if(!(date instanceof Date)||!Number.isFinite(date.getTime()))throw new TypeError('date inválida');
  if(!Number.isFinite(longitude)||longitude < -180||longitude>360)throw new RangeError('longitud fuera de rango');
  const jd=date.getTime()/86400000+2440587.5;
  const msd=(jd-MSD_EPOCH)/MARS_DAY+44796-0.0009626;
  const lmstHours=((msd-Math.floor(msd))*24+longitude/15%24+24)%24;
  return {msd,sol:Math.floor(msd),lmstHours,longitude};
 }
 function init(){
  const earth=document.getElementById('earth-clock'),mars=document.getElementById('mars-clock');
  if(earth&&mars){const tick=()=>{const now=new Date(),m=marsClock(now);earth.textContent=now.toISOString().slice(11,19)+' UTC';mars.textContent=`MSD ${m.sol} · ${Math.floor(m.lmstHours).toString().padStart(2,'0')}:${Math.floor(m.lmstHours%1*60).toString().padStart(2,'0')}:${Math.floor(m.lmstHours*3600%60).toString().padStart(2,'0')} LMST`;};tick();root.setInterval(tick,1000);}
  const input=document.getElementById('bitacora-entry'),tags=document.getElementById('bitacora-tags'),filter=document.getElementById('bitacora-filter'),save=document.getElementById('bitacora-save'),list=document.getElementById('bitacora-list'),status=document.getElementById('bitacora-status');
  if(!input||!tags||!save||!list)return;
  const key='ruta-marciana:hub-bitacora:v1';let notes=[];
  try{const parsed=JSON.parse(root.localStorage.getItem(key)||'[]');if(Array.isArray(parsed))notes=parsed.filter(n=>n&&typeof n.text==='string'&&typeof n.created==='string'&&Array.isArray(n.tags)).slice(0,100);}catch{}
  const render=()=>{list.replaceChildren();const query=filter?.value.trim().toLocaleLowerCase()||'';for(const note of notes.filter(n=>!query||n.tags.some(t=>typeof t==='string'&&t.toLocaleLowerCase().includes(query)))){const item=document.createElement('li'),text=document.createElement('p'),meta=document.createElement('small'),remove=document.createElement('button');text.textContent=note.text;meta.textContent=`${new Date(note.created).toLocaleString()} · ${note.tags.join(', ')||'sin tags'}`;remove.type='button';remove.textContent='Eliminar';remove.setAttribute('aria-label','Eliminar nota');remove.addEventListener('click',()=>{notes=notes.filter(n=>n!==note);persist();});item.append(text,meta,remove);list.append(item);}};
  const persist=()=>{try{root.localStorage.setItem(key,JSON.stringify(notes));status.textContent='Guardado solo en este navegador.';}catch{status.textContent='No se pudo guardar; la nota no se conservó.';}render();};
  save.addEventListener('click',()=>{const text=input.value.trim(),parsed=tags.value.split(',').map(t=>t.trim().slice(0,32)).filter(Boolean).slice(0,8);if(!text){status.textContent='Escribe una nota antes de guardar.';input.focus();return;}if(notes.length>=100){status.textContent='Límite local: 100 notas. Elimina una antes de añadir otra.';return;}notes.unshift({text:text.slice(0,1000),tags:[...new Set(parsed)],created:new Date().toISOString()});input.value='';tags.value='';persist();});
  if(filter)filter.addEventListener('input',render);
  render();
 }
 const api={marsClock};root.MarsHub=api;if(typeof document!=='undefined')document.addEventListener('DOMContentLoaded',init);
})(typeof window!=='undefined'?window:globalThis);
