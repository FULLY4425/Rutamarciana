/* Global atlas and region selection; navigation remains in the existing explorer. */
(function(root){
  'use strict';
  const radians=value=>value*Math.PI/180;
  function project(lat,lon,yaw,pitch){
    const latitude=radians(lat),longitude=radians(lon)-yaw;
    const flat=Math.cos(latitude)*Math.cos(longitude);
    return [Math.cos(latitude)*Math.sin(longitude),Math.sin(latitude)*Math.cos(pitch)-flat*Math.sin(pitch),
      Math.sin(latitude)*Math.sin(pitch)+flat*Math.cos(pitch)];
  }
  function unproject(x,y,yaw,pitch){
    if(x*x+y*y>1)return null;
    const z=Math.sqrt(Math.max(0,1-x*x-y*y));
    const lat=Math.asin(Math.max(-1,Math.min(1,y*Math.cos(pitch)+z*Math.sin(pitch))));
    const lon=yaw+Math.atan2(x,z*Math.cos(pitch)-y*Math.sin(pitch));
    return {lat:lat*180/Math.PI,lon:((lon*180/Math.PI+180)%360+360)%360-180};
  }
  if(typeof module==='object' && module.exports){module.exports={project,unproject};return;}
  const el=id=>document.getElementById(id),canvas=el('planetcanvas'),ctx=canvas.getContext('2d');
  let selectedPreset=null;
  let data=null,open=false,yaw=radians(77),pitch=.25,drag=null,moved=false,queued=false;
  const radius=390,cx=480,cy=470;
  function draw(){
    queued=false;if(!open)return;
    ctx.fillStyle='#0b1314';ctx.fillRect(0,0,1000,1000);
    if(!data){ctx.fillStyle='#a8c2b4';ctx.font='18px system-ui';ctx.fillText('Cargando elevación global MOLA…',240,400);return;}
    const vertices=data.latitudes.map((lat,r)=>data.longitudes.map((lon,c)=>{
      const point=project(lat,lon,yaw,pitch),relief=1+data.elevations[r][c]/3396000;
      return {point,x:cx+point[0]*radius*relief,y:cy-point[1]*radius*relief,height:data.elevations[r][c]};
    }));
    const faces=[];
    for(let r=0;r<vertices.length-1;r++)for(let c=0;c<vertices[0].length-1;c++){
      const points=[vertices[r][c],vertices[r+1][c],vertices[r+1][c+1],vertices[r][c+1]];
      if(points.some(p=>p.point[2]<0))continue;
      faces.push({points,depth:points.reduce((sum,p)=>sum+p.point[2],0)/4});
    }
    faces.sort((a,b)=>a.depth-b.depth);
    for(const face of faces){
      const height=face.points.reduce((sum,p)=>sum+p.height,0)/4,t=Math.max(0,Math.min(1,(height+8200)/29500));
      const center=face.points[0].point;
      const light=.38+.62*Math.max(0,(-.4*center[0]+.5*center[1]+.8*center[2])/Math.hypot(.4,.5,.8));
      ctx.fillStyle=`rgb(${Math.round((90+150*t)*light)},${Math.round((100+80*t)*light)},${Math.round((100+20*t)*light)})`;
      ctx.beginPath();face.points.forEach((p,i)=>i?ctx.lineTo(p.x,p.y):ctx.moveTo(p.x,p.y));ctx.closePath();ctx.fill();ctx.strokeStyle=ctx.fillStyle;ctx.lineWidth=.7;ctx.stroke();
    }
    for(const bookmark of data.bookmarks){
      const p=project(bookmark.lat,bookmark.lon,yaw,pitch);if(p[2]<.05)continue;
      const x=cx+p[0]*radius,y=cy-p[1]*radius;
      ctx.fillStyle=bookmark.region==='jezero'?'#f18559':'#a3decb';ctx.beginPath();ctx.arc(x,y,4,0,Math.PI*2);ctx.fill();
      ctx.font='12px system-ui';ctx.fillText(bookmark.name.split(' · ')[0],x+9,y-8);
    }
    const selected=project(Number(el('planetlat').value),Number(el('planetlon').value),yaw,pitch);
    if(selected[2]>0){ctx.strokeStyle='#fff0cb';ctx.lineWidth=2;ctx.beginPath();ctx.arc(cx+selected[0]*radius,cy-selected[1]*radius,9,0,Math.PI*2);ctx.stroke();}
  }
  function schedule(){if(!queued){queued=true;requestAnimationFrame(draw);}}
  function setOpen(value){open=value;el('planetview').hidden=!value;el('planetopen').setAttribute('aria-pressed',String(value));if(open){root.dispatchEvent(new CustomEvent('atlas-open'));schedule();canvas.focus();}else el('planetopen').focus();}
  function choose(region){setOpen(false);root.dispatchEvent(new CustomEvent('mission-region',{detail:{region}}));}
  el('planetopen').onclick=()=>setOpen(true);el('planetclose').onclick=()=>setOpen(false);
  el('planetjezero').onclick=()=>choose('jezero');
  el('planetform').onsubmit=event=>{
    event.preventDefault();
    const lat=Number(el('planetlat').value),lon=Number(el('planetlon').value);
    if(!Number.isFinite(lat)||!Number.isFinite(lon)||Math.abs(lat)>90){el('planetstatus').textContent='Revisa las coordenadas.';return;}
    choose(selectedPreset==='jezero'?'jezero':`mola:${lat.toFixed(4)}:${lon.toFixed(4)}`);
  };
  el('planetbookmark').onchange=event=>{
    const bookmark=data?.bookmarks[Number(event.target.value)];if(!bookmark)return;
    selectedPreset=bookmark.region||null;
    el('planetload').textContent=selectedPreset==='jezero'?'Abrir Jezero · HiRISE 4 m':'Abrir región MOLA · 400 km';
    el('planetlat').value=bookmark.lat;el('planetlon').value=bookmark.lon;
    yaw=radians(bookmark.lon);pitch=radians(Math.max(-85,Math.min(85,bookmark.lat)));
    el('planetstatus').textContent=bookmark.region==='jezero'?'El acceso Jezero abrirá el recorte HiRISE de 4 m.':`${bookmark.name}. Abre la región para explorar y ensayar un recorrido.`;schedule();
  };
  for(const name of ['planetlat','planetlon'])el(name).oninput=()=>{selectedPreset=null;el('planetload').textContent='Abrir región MOLA · 400 km';schedule();};
  canvas.style.touchAction='none';
  canvas.addEventListener('pointerdown',event=>{drag={x:event.clientX,y:event.clientY,startX:event.clientX,startY:event.clientY};moved=false;canvas.setPointerCapture(event.pointerId);});
  canvas.addEventListener('pointermove',event=>{
    if(!drag)return;if(Math.hypot(event.clientX-drag.startX,event.clientY-drag.startY)>5)moved=true;
    if(moved){yaw-=(event.clientX-drag.x)*.008;pitch=Math.max(-Math.PI/2,Math.min(Math.PI/2,pitch+(event.clientY-drag.y)*.006));schedule();}
    drag.x=event.clientX;drag.y=event.clientY;
  });
  canvas.addEventListener('pointercancel',()=>{drag=null;});
  canvas.addEventListener('pointerup',event=>{
    if(!drag)return;drag=null;if(moved)return;
    const rect=canvas.getBoundingClientRect(),x=((event.clientX-rect.left)/rect.width*1000-cx)/radius,y=(cy-(event.clientY-rect.top)/rect.height*1000)/radius;
    const point=unproject(x,y,yaw,pitch);if(!point)return;
    selectedPreset=null;el('planetload').textContent='Abrir región MOLA · 400 km';
    el('planetlat').value=point.lat.toFixed(4);el('planetlon').value=point.lon.toFixed(4);
    el('planetstatus').textContent=`Selección aproximada: ${point.lat.toFixed(2)}°, ${point.lon.toFixed(2)}° E. Abre la región para planificar.`;schedule();
  });
  canvas.addEventListener('keydown',event=>{
    if(event.key==='Escape'){setOpen(false);return;}
    const actions={ArrowLeft:()=>yaw-=.1,ArrowRight:()=>yaw+=.1,ArrowUp:()=>pitch=Math.min(Math.PI/2,pitch+.1),ArrowDown:()=>pitch=Math.max(-Math.PI/2,pitch-.1)};
    if(actions[event.key]){event.preventDefault();actions[event.key]();schedule();}
  });
  setOpen(!new URLSearchParams(location.search).has('region'));
  fetch('/api/mars/atlas').then(response=>{if(!response.ok)throw Error('No se pudo cargar el atlas global.');return response.json();}).then(value=>{
    data=value;
    data.bookmarks.forEach((bookmark,index)=>{const option=document.createElement('option');option.value=index;option.textContent=bookmark.name;el('planetbookmark').appendChild(option);});
    el('planetstatus').textContent='Arrastra para girar, pulsa para elegir una zona o introduce coordenadas. Las alturas son MOLA; los colores no son fotografía orbital.';schedule();
  }).catch(error=>{el('planetstatus').textContent=error.message;});
})(typeof window==='object'?window:globalThis);
