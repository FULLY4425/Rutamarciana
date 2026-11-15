/* Two virtual walkers: aerial symbols and meter-scale meshes for the surface view. */
(function(root){
 function geometry(position,heading,phase){
  const vertices=[],cos=Math.cos(heading),sin=Math.sin(heading);
  function box(cx,cy,cz,w,h,d){
   const points=[[-w/2,-h/2,-d/2],[w/2,-h/2,-d/2],[w/2,h/2,-d/2],[-w/2,h/2,-d/2],[-w/2,-h/2,d/2],[w/2,-h/2,d/2],[w/2,h/2,d/2],[-w/2,h/2,d/2]];
   for(const face of [[0,3,2,1],[4,5,6,7],[0,4,7,3],[1,2,6,5],[3,7,6,2],[0,1,5,4]])for(const i of [0,1,2,0,2,3]){
    const p=points[face[i]],x=cx+p[0],z=cz+p[2];vertices.push(position[0]+x*cos-z*sin,position[1]+cy+p[1],position[2]+x*sin+z*cos,0,1,0,0,0);
   }
  }
  box(0,1.05,0,.5,.62,.3);box(0,1.57,0,.4,.4,.4);box(0,1,.26,.42,.48,.22);
  for(const side of [-1,1]){box(side*.15,.4,Math.sin(phase)*side*.13,.19,.78,.22);box(side*.37,1.03,-Math.sin(phase)*side*.12,.17,.65,.19);}
  return vertices;
 }
 function placements(path,sample,terrain,elevation,samplePath){
  return [0,1].map(index=>{
   const distance=Math.max(0,sample.distance-index*8),here=samplePath(path,distance/(sample.total||1));
   const ahead=samplePath(path,Math.min(sample.total,distance+2)/(sample.total||1));
   const height=elevation(here.point);
   return {point:here.point,world:[here.point[1]*terrain.cell_x_m,height,here.point[0]*terrain.cell_m],heading:Math.atan2((ahead.point[1]-here.point[1])*terrain.cell_x_m,-(ahead.point[0]-here.point[0])*terrain.cell_m),phase:distance*2.5,index};
  });
 }
 function paint(ctx,actors,project,running){
  let previous=null;
  for(const actor of actors){const p=project(actor.point);let x=p[0],y=p[1];if(previous&&Math.hypot(x-previous[0],y-previous[1])<22)x+=22;previous=[x,y];
   ctx.save();ctx.translate(x,y);ctx.strokeStyle=actor.index?'#faab6a':'#edf4e5';ctx.fillStyle=ctx.strokeStyle;ctx.lineWidth=3;
   const step=running?Math.sin(actor.phase)*3:0;ctx.beginPath();ctx.moveTo(-3,0);ctx.lineTo(-4-step,8);ctx.moveTo(3,0);ctx.lineTo(4+step,8);ctx.moveTo(-5,-9);ctx.lineTo(-9+step,-2);ctx.moveTo(5,-9);ctx.lineTo(9-step,-2);ctx.stroke();ctx.fillRect(-5,-13,10,14);ctx.beginPath();ctx.arc(0,-18,6,0,Math.PI*2);ctx.fill();ctx.fillStyle='#172f37';ctx.fillRect(-4,-20,8,4);ctx.restore();
  }
 }
 const api={geometry,placements,paint};if(typeof module==='object'&&module.exports)module.exports=api;else root.FieldActors=api;
})(typeof window==='object'?window:globalThis);
