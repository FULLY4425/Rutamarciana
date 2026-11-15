/* Route geometry and directions: no DOM, speech or terrain rendering dependencies. */
(function (root) {
  'use strict';
  const distance = (a, b, scale) => Math.hypot((b[0]-a[0])*scale.y, (b[1]-a[1])*scale.x);

  function simplify(points, scale, tolerance = 10) {
    if (points.length < 3) return points.map((_, i) => i);
    const keep = new Set([0, points.length-1]), stack = [[0, points.length-1]];
    while (stack.length) {
      const [first,last] = stack.pop(), a=points[first], b=points[last];
      const dx=(b[1]-a[1])*scale.x, dy=(b[0]-a[0])*scale.y, length=dx*dx+dy*dy;
      let farthest=-1, max=tolerance;
      for (let i=first+1;i<last;i++) {
        const px=(points[i][1]-a[1])*scale.x, py=(points[i][0]-a[0])*scale.y;
        const t=length ? Math.max(0,Math.min(1,(px*dx+py*dy)/length)) : 0;
        const offset=Math.hypot(px-t*dx,py-t*dy);
        if (offset>max) {max=offset;farthest=i;}
      }
      if (farthest!==-1) {keep.add(farthest);stack.push([first,farthest],[farthest,last]);}
    }
    return [...keep].sort((a,b)=>a-b);
  }

  function build(route, scale) {
    if (!route) return {steps:[],total:0};
    const outbound=route.outbound;
    const legs=outbound?.legs?.length ? outbound.legs : [{points:outbound?.points || route.points || []}];
    const segments=legs.map((leg,i)=>({points:leg.points,arrival:i===legs.length-1?'destination':'science',stop:i+1}));
    if (route.return?.points?.length>1) segments.push({points:route.return.points,arrival:'home'});
    const steps=[];
    let offset=0;
    for (let segment=0;segment<segments.length;segment++) {
      const {points,arrival,stop}=segments[segment];
      if (!points || points.length<2) continue;
      const cumulative=[0];
      for(let i=1;i<points.length;i++) cumulative.push(cumulative[i-1]+distance(points[i-1],points[i],scale));
      if (!steps.length) steps.push({at:0,kind:'depart',text:'Inicia el recorrido desde A.',symbol:'↑'});
      const corners=simplify(points,scale);
      for(let i=1;i<corners.length-1;i++) {
        const a=points[corners[i-1]],b=points[corners[i]],c=points[corners[i+1]];
        const ax=(b[1]-a[1])*scale.x,ay=-(b[0]-a[0])*scale.y;
        const bx=(c[1]-b[1])*scale.x,by=-(c[0]-b[0])*scale.y;
        const angle=Math.atan2(ax*by-ay*bx,ax*bx+ay*by)*180/Math.PI;
        if (Math.abs(angle)<30) continue;
        const kind=Math.abs(angle)>150?'uturn':angle>0?'left':'right';
        steps.push({at:offset+cumulative[corners[i]],kind,
          text:kind==='uturn'?'Cambia de sentido siguiendo la ruta.':kind==='left'?'Gira a la izquierda.':'Gira a la derecha.',
          symbol:kind==='uturn'?'↶':kind==='left'?'↰':'↱'});
      }
      offset+=cumulative.at(-1);
      steps.push({at:offset,kind:arrival,symbol:arrival==='home'?'⌂':arrival==='science'?'⚑':'◎',
        text:arrival==='home'?'Has regresado al punto A. Recorrido completado.':arrival==='science'?`Llegada a la parada científica S${stop}.`:'Llegada al destino B. Inicia el regreso por la ruta turquesa.'});
    }
    return {steps,total:offset};
  }
  function atPosition(plan, traveled) {
    if (!plan.steps.length) return null;
    const position=Math.max(0,Math.min(plan.total,traveled));
    let current=0;
    for(let i=0;i<plan.steps.length;i++) if(plan.steps[i].at<=position+0.01) current=i;
    const next=plan.steps[current+1] || null;
    return {current,step:plan.steps[current],next,remaining:Math.max(0,plan.total-position),
      distance:next?Math.max(0,next.at-position):0};
  }
  const api={build,atPosition};
  if(typeof module==='object' && module.exports) module.exports=api;
  else root.MissionNavigation=api;
})(typeof window==='object'?window:globalThis);
