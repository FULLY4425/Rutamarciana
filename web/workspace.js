/* Group the existing tools without duplicating controls or changing mission state. */
(function(root){
 'use strict';
 const workspace=document.querySelector('.workspace'),map=document.getElementById('map-section');
 const desktop=root.matchMedia('(min-width:1001px)'),groups=[];let queued=false,current=null;
 function make(tag,className,text){const node=document.createElement(tag);if(className)node.className=className;if(text)node.textContent=text;return node;}
 const dialog=make('dialog','workspace-dialog'),heading=make('header','workspace-dialog-head');
 const title=make('h2','', 'Herramientas de misión');title.id='workspace-dialog-title';dialog.setAttribute('aria-labelledby',title.id);
 const close=make('button','','Volver al mapa');close.type='button';const body=make('div','workspace-dialog-body');
 heading.append(title,close);dialog.append(heading,body);document.body.append(dialog);
 close.onclick=()=>dialog.close();
 function schedule(){if(!queued){queued=true;requestAnimationFrame(layout);}}
 function restore(){
  if(!current)return;const owner=current;current=null;owner.page.prepend(owner.content);schedule();owner.expand.focus({preventScroll:true});
 }
 dialog.addEventListener('close',()=>{if(!dialog.open)restore();});
 function dismiss(){if(dialog.open){dialog.close();restore();}}
 function expand(group){
  if(current===group)return;
  dismiss();current=group;group.content.hidden=false;title.textContent=group.name;body.append(group.content);dialog.showModal();body.scrollTop=0;
 }
 function choose(group,focus=false){
  for(const other of groups.filter(g=>g.side===group.side)){
   const active=other===group;other.active=active;other.page.hidden=!active;other.tab.setAttribute('aria-selected',String(active));other.tab.tabIndex=active?0:-1;other.expand.hidden=!active;
  }
  if(focus)group.tab.focus();schedule();
 }
 function side(selector,key,definitions){
  const panel=document.querySelector(selector),tabs=make('div','workspace-tabs'),pages=make('div','workspace-pages'),actions=make('div','workspace-page-actions');
  tabs.setAttribute('role','tablist');tabs.setAttribute('aria-label',key==='plan'?'Herramientas de planificación':'Herramientas del recorrido');
  for(const [id,name,description,selectors] of definitions){
   const tab=make('button','',name),page=make('section','workspace-page'),content=make('div','workspace-page-content'),overview=make('div','workspace-overview'),expandButton=make('button','workspace-expand','Ampliar sección');
   tab.type='button';tab.id='tab-'+id;tab.setAttribute('role','tab');tab.setAttribute('aria-controls','page-'+id);
   page.id='page-'+id;page.setAttribute('role','tabpanel');page.setAttribute('aria-labelledby',tab.id);page.tabIndex=0;
   overview.append(make('strong','',name),make('p','',description),make('p','workspace-overview-note','Abre la sección completa para consultar y editar todos sus detalles.'));overview.hidden=true;
   for(const selector of selectors){const node=document.querySelector(selector);if(node)content.append(node);}
   expandButton.type='button';expandButton.setAttribute('aria-haspopup','dialog');
   page.append(content,overview);tabs.append(tab);pages.append(page);actions.append(expandButton);
   const group={id,name,side:key,panel,tab,page,content,overview,expand:expandButton,active:false};groups.push(group);
   tab.onclick=()=>choose(group);expandButton.onclick=()=>expand(group);
   tab.onkeydown=event=>{
    const siblings=groups.filter(g=>g.side===key),index=siblings.indexOf(group);let next;
    if(event.key==='ArrowRight')next=siblings[(index+1)%siblings.length];else if(event.key==='ArrowLeft')next=siblings[(index+siblings.length-1)%siblings.length];else if(event.key==='Home')next=siblings[0];else if(event.key==='End')next=siblings.at(-1);
    if(next){event.preventDefault();choose(next,true);}
   };
  }
  panel.append(tabs,pages,actions);choose(groups.find(g=>g.side===key));
 }
 side('.plan-panel','plan',[
  ['plan','Plan','Presupuesto EVA, paradas, tiempo científico y pendiente máxima.',['.controls','.point-key','#mockflag']],
  ['science','Ciencia','Objetivo, hipótesis, equipo transportado y factor de marcha.',['.science-mission']],
  ['points','Puntos','Objetivos científicos, puntos personalizados y orden del recorrido.',['.point-tools']],
  ['data','Datos','Procedencia del terreno, fotografía y observaciones químicas ChemCam.',['.terrain-facts','.chemistry-layer']],
  ['watch','Reloj','Intercambio de misiones con el firmware Mars OS del ESP32.',['.psion-exchange']]
 ]);
 side('.details-panel','journey',[
  ['guide','Guía','Próxima maniobra, distancias e indicaciones de voz.',['#mission-guidance']],
  ['playback','Recorrido','Astronautas, reproducción, exportación y perfil de elevación.',['.tracking','.elevation-profile']],
  ['environment','Entorno','Escenarios de polvo, posición simulada y recálculo.',['.field-operations']],
  ['results','Rutas','Comparación FAST/SAFE, tramos y presupuesto EVA completo.',['#route-results']],
  ['notes','Notas','Observaciones propias registradas por parada.',['.mission-notebook']],
  ['sources','Fuentes','Fuentes NASA/USGS, ortofoto de referencia y alcance del modelo.',['.science-sources','.reference','.note','.mission-footer']]
 ]);
 function layout(){
  queued=false;const height=map.getBoundingClientRect().height;workspace.style.setProperty('--workspace-map-height',height+'px');
  for(const group of groups){
   if(!group.active||current===group)continue;
   group.content.hidden=false;group.overview.hidden=true;
   const available=group.page.parentElement.clientHeight;
   const compact=desktop.matches&&group.content.getBoundingClientRect().height>available+1;
   if(compact&&group.content.contains(document.activeElement)){expand(group);continue;}
   group.content.hidden=compact;group.overview.hidden=!compact;
  }
 }
 function reveal(target){
  if(!target)return;
  const group=target.id==='mission-controls'?groups.find(g=>g.id==='plan'):groups.find(g=>g.page.contains(target)||g.content.contains(target));
  if(!group){dismiss();return;}
  if(current&&current!==group)dismiss();choose(group);layout();
  if(group.content.hidden)expand(group);
 }
 root.WorkspaceSections={reveal,dismiss};
 document.addEventListener('click',event=>{
  const link=event.target.closest('a[href^="#"]');if(!link)return;
  const target=document.getElementById(link.getAttribute('href').slice(1));if(!target)return;
  if(target.id==='mission-controls'||groups.some(g=>g.page.contains(target)||g.content.contains(target))){event.preventDefault();reveal(target);target.scrollIntoView({block:'start',behavior:'smooth'});}
 });
 const resize=new ResizeObserver(schedule);resize.observe(map);resize.observe(workspace);for(const group of groups)resize.observe(group.content);
 document.addEventListener('toggle',event=>{const group=groups.find(g=>g.content.contains(event.target));if(!group)return;requestAnimationFrame(()=>{if(desktop.matches&&event.target.open&&group.active&&!current&&group.content.getBoundingClientRect().height>group.page.parentElement.clientHeight)expand(group);else schedule();});},true);
 const mutations=new MutationObserver(records=>{if(records.some(r=>r.target.parentElement?.closest('.workspace-page-content')||r.target.closest?.('.workspace-page-content')))schedule();});
 mutations.observe(workspace,{subtree:true,childList:true,characterData:true});
 root.addEventListener('resize',schedule);root.addEventListener('region-ready',schedule);
 workspace.classList.add('workspace-grouped');layout();
 if(location.hash){const target=document.getElementById(location.hash.slice(1));if(target)reveal(target);}
})(window);
