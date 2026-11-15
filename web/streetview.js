/* Surface reconstruction. Geometry and imagery never change the planned route. */
(function(root) {
  'use strict';
  function alignedTexture(terrain, reference) {
    if(!terrain || !reference)return false;
    const spatial=terrain.spatial;
    return terrain?.site!=='synth' && spatial?.crs===reference?.crs &&
      Array.isArray(spatial.bounds) && spatial.bounds.length===4 &&
      spatial.bounds.every((value,i)=>Math.abs(value-reference.bounds_m?.[i])<0.1) &&
      ['row','col','height','width'].every(key=>spatial.source_window?.[key]===reference.dem_source_window?.[key]);
  }
  function heading(a,b,scale) {
    return Math.atan2((b[1]-a[1])*scale.x,-(b[0]-a[0])*scale.y);
  }
  // Match the triangles used by the GPU, so the eye never sinks below a diagonal.
  function surfaceHeight(terrain, point) {
    const {rows,cols,elevations:z}=terrain;
    const r=rows.findIndex(value=>value>=point[0]), c=cols.findIndex(value=>value>=point[1]);
    if(r<0 || c<0 || point[0]<rows[0] || point[1]<cols[0])return null;
    const r0=Math.max(0,r-1), c0=Math.max(0,c-1);
    const t=(point[0]-rows[r0])/(rows[r]-rows[r0]||1),u=(point[1]-cols[c0])/(cols[c]-cols[c0]||1);
    const samples=t+u<=1?[[z[r0][c0],1-t-u],[z[r][c0],t],[z[r0][c],u]]:
      [[z[r][c],t+u-1],[z[r0][c],1-t],[z[r][c0],1-u]];
    if(samples.some(([value,weight])=>weight>0 && value===null))return null;
    return samples.reduce((total,[value,weight])=>total+(value??0)*weight,0);
  }
  function mesh(terrain) {
    const {rows,cols,elevations:z}=terrain, vertices=[],indices=[];
    for(let r=0;r<rows.length;r++) for(let c=0;c<cols.length;c++) {
      const left=Math.max(0,c-1),right=Math.min(cols.length-1,c+1);
      const top=Math.max(0,r-1),bottom=Math.min(rows.length-1,r+1);
      const dx=((z[r][right]??z[r][c])-(z[r][left]??z[r][c]))/((cols[right]-cols[left])*terrain.cell_x_m||1);
      const dz=((z[bottom][c]??z[r][c])-(z[top][c]??z[r][c]))/((rows[bottom]-rows[top])*terrain.cell_m||1);
      const length=Math.hypot(dx,1,dz);
      vertices.push(cols[c]*terrain.cell_x_m,z[r][c]??0,rows[r]*terrain.cell_m,
        -dx/length,1/length,-dz/length,cols[c]/(terrain.grid.cols-1),rows[r]/(terrain.grid.rows-1));
    }
    for(let r=0;r<rows.length-1;r++) for(let c=0;c<cols.length-1;c++) {
      for(const cells of [[[r,c],[r+1,c],[r,c+1]],[[r+1,c],[r+1,c+1],[r,c+1]]]) {
        if(cells.every(([y,x])=>z[y][x]!==null))indices.push(...cells.map(([y,x])=>y*cols.length+x));
      }
    }
    return {vertices,indices};
  }
  const geometry={alignedTexture,heading,mesh,surfaceHeight};
  if(typeof module==='object' && module.exports){module.exports=geometry;return;}
  root.SurfaceGeometry=geometry;
  const el=id=>document.getElementById(id), canvas=el('surfacecanvas');
  const gl=canvas.getContext('webgl',{alpha:true,antialias:true});
  let open=false, lost=false, model=null, count=0, textureReady=false, textureRequested=false;
  let latest=null, yawOffset=0, pitch=0, fov=75, drag=null, reference=null;
  let textureGeneration=0, program,vertexBuffer,indexBuffer,routeBuffer,texture,actorBuffer;
  let referenceRegion=null,referenceRequest=0;
  function status(text){el('surfacestatus').textContent=text;}
  if(!gl){el('surfaceopen').disabled=true;status('La vista de superficie requiere WebGL. El mapa y las indicaciones siguen disponibles.');return;}
  const vertex=`attribute vec3 position;attribute vec3 normal;attribute vec2 uv;
    uniform vec3 camera;uniform float yaw;uniform float pitch;uniform float focal;uniform float aspect;
    varying vec3 vNormal;varying vec2 vUv;varying float vDepth;varying vec3 vWorld;
    void main(){vec3 p=position-camera;float d=p.x*sin(yaw)-p.z*cos(yaw);
      float x=p.x*cos(yaw)+p.z*sin(yaw);float y=p.y*cos(pitch)-d*sin(pitch);
      float depth=d*cos(pitch)+p.y*sin(pitch);float near=0.15;float far=12000.0;
      gl_Position=vec4(x*focal/aspect,y*focal,((far+near)*depth-2.0*far*near)/(far-near),depth);
      vNormal=normal;vUv=uv;vDepth=length(p);vWorld=position;}`;
  const fragment=`precision mediump float;varying vec3 vNormal;varying vec2 vUv;varying float vDepth;varying vec3 vWorld;
    uniform sampler2D ortho;uniform bool textured;uniform bool route;uniform bool actor;uniform vec3 suit;uniform float fogDensity;
    void main(){
      vec3 haze=vec3(0.63,0.40,0.29);vec3 color;
      if(actor){color=suit;}else if(route){color=vec3(0.40,0.94,0.88);}else{
        float light=0.38+0.62*max(0.0,dot(normalize(vNormal),normalize(vec3(-0.6,0.8,-0.3))));
        float grain=fract(sin(dot(floor(vWorld.xz*2.0),vec2(12.9898,78.233)))*43758.5453);
        vec3 ground=textured?texture2D(ortho,vUv).rgb:vec3(0.65,0.39,0.24)*(0.94+grain*0.12);
        color=ground*light;
      }
      float fog=1.0-exp(-vDepth*fogDensity);gl_FragColor=vec4(mix(color,haze,min(fog,0.94)),1.0);
    }`;
  function shader(type,source){const s=gl.createShader(type);gl.shaderSource(s,source);gl.compileShader(s);if(!gl.getShaderParameter(s,gl.COMPILE_STATUS))throw Error('No se pudo compilar la vista 3D.');return s;}
  try {
    program=gl.createProgram();gl.attachShader(program,shader(gl.VERTEX_SHADER,vertex));gl.attachShader(program,shader(gl.FRAGMENT_SHADER,fragment));gl.linkProgram(program);
    if(!gl.getProgramParameter(program,gl.LINK_STATUS))throw Error('No se pudo iniciar la vista 3D.');
    vertexBuffer=gl.createBuffer();indexBuffer=gl.createBuffer();routeBuffer=gl.createBuffer();actorBuffer=gl.createBuffer();texture=gl.createTexture();
    gl.useProgram(program);gl.enable(gl.DEPTH_TEST);gl.bindTexture(gl.TEXTURE_2D,texture);
    gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,1,1,0,gl.RGBA,gl.UNSIGNED_BYTE,new Uint8Array([160,100,60,255]));
    gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.LINEAR);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_S,gl.CLAMP_TO_EDGE);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_T,gl.CLAMP_TO_EDGE);
  }catch(error){el('surfaceopen').disabled=true;status(error.message);return;}
  const uniform=name=>gl.getUniformLocation(program,name);
  function attributes(buffer){gl.bindBuffer(gl.ARRAY_BUFFER,buffer);for(const [name,size,offset] of [['position',3,0],['normal',3,12],['uv',2,24]]){const location=gl.getAttribLocation(program,name);gl.enableVertexAttribArray(location);gl.vertexAttribPointer(location,size,gl.FLOAT,false,32,offset);}}
  function loadTexture(){
    if(textureRequested || !reference || !model)return;
    textureRequested=true;
    if(!alignedTexture(model,reference))return;
    const generation=textureGeneration, image=new Image();
    image.onload=()=>{if(generation!==textureGeneration || lost)return;gl.bindTexture(gl.TEXTURE_2D,texture);gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,gl.RGBA,gl.UNSIGNED_BYTE,image);textureReady=true;render();};
    image.onerror=()=>{if(generation===textureGeneration){textureReady=false;render();}};
    image.src=typeof apiURL==='function'?apiURL('/reference-image'):'/reference-image';
  }
  function render(){
    if(!open || lost || !latest)return;
    const {terrain,path,sample,ahead}=latest;
    if(!terrain || !sample || path.length<2){status('Calcula una ruta y carga el terreno para entrar a la superficie.');gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);return;}
    if(model!==terrain){model=terrain;const data=mesh(terrain);count=data.indices.length;textureReady=false;textureRequested=false;textureGeneration++;
      gl.bindBuffer(gl.ARRAY_BUFFER,vertexBuffer);gl.bufferData(gl.ARRAY_BUFFER,new Float32Array(data.vertices),gl.STATIC_DRAW);
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER,indexBuffer);gl.bufferData(gl.ELEMENT_ARRAY_BUFFER,new Uint16Array(data.indices),gl.STATIC_DRAW);
    }
    loadTexture();
    const altitude=surfaceHeight(terrain,sample.point);
    if(altitude===null){gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);status('Sin elevación válida en esta posición. Mueve el avance a otra parte de la ruta.');return;}
    const rect=canvas.getBoundingClientRect(),ratio=Math.min(root.devicePixelRatio||1,2);
    const width=Math.max(1,Math.round(rect.width*ratio)),height=Math.max(1,Math.round(rect.height*ratio));
    if(canvas.width!==width || canvas.height!==height){canvas.width=width;canvas.height=height;}
    gl.viewport(0,0,width,height);gl.clearColor(0,0,0,0);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);gl.useProgram(program);
    const direction=sample.distance<sample.total?[sample.point,ahead.point]:[ahead.point,sample.point];
    const routeHeading=heading(...direction,{x:terrain.cell_x_m,y:terrain.cell_m});
    gl.uniform3f(uniform('camera'),sample.point[1]*terrain.cell_x_m,altitude+1.7,sample.point[0]*terrain.cell_m);
    gl.uniform1f(uniform('yaw'),routeHeading+yawOffset);gl.uniform1f(uniform('pitch'),pitch);gl.uniform1f(uniform('focal'),1/Math.tan(fov*Math.PI/360));gl.uniform1f(uniform('aspect'),width/height);
    gl.uniform1i(uniform('actor'),0);gl.uniform1f(uniform('fogDensity'),latest.environment?.weather==='storm'?.0036:latest.environment?.weather==='dust'?.0022:.0012);
    gl.uniform1i(uniform('textured'),textureReady?1:0);gl.uniform1i(uniform('route'),0);
    attributes(vertexBuffer);gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER,indexBuffer);gl.drawElements(gl.TRIANGLES,count,gl.UNSIGNED_SHORT,0);
    const routeVertices=[],strips=[];let start=0;
    for(const point of path){
      const z=surfaceHeight(terrain,point);
      if(z===null){if(routeVertices.length/8>start)strips.push([start,routeVertices.length/8-start]);start=routeVertices.length/8;continue;}
      routeVertices.push(point[1]*terrain.cell_x_m,z+0.18,point[0]*terrain.cell_m,0,1,0,0,0);
    }
    if(routeVertices.length/8>start)strips.push([start,routeVertices.length/8-start]);
    gl.bindBuffer(gl.ARRAY_BUFFER,routeBuffer);gl.bufferData(gl.ARRAY_BUFFER,new Float32Array(routeVertices),gl.DYNAMIC_DRAW);
    attributes(routeBuffer);gl.uniform1i(uniform('route'),1);for(const [first,length] of strips)gl.drawArrays(gl.LINE_STRIP,first,length);
    if(root.FieldActors && latest.actors){
      gl.uniform1i(uniform('route'),0);gl.uniform1i(uniform('actor'),1);
      for(const actor of latest.actors){if(actor.world[1]===null)continue;
        const vertices=root.FieldActors.geometry([actor.world[0],geometry.surfaceHeight(terrain,actor.point)??actor.world[1],actor.world[2]],actor.heading,actor.phase);
        gl.bindBuffer(gl.ARRAY_BUFFER,actorBuffer);gl.bufferData(gl.ARRAY_BUFFER,new Float32Array(vertices),gl.DYNAMIC_DRAW);attributes(actorBuffer);
        gl.uniform3f(uniform('suit'),...(actor.index?[.97,.61,.34]:[.93,.95,.87]));gl.drawArrays(gl.TRIANGLES,0,vertices.length/8);
      }
    }
    const degrees=((routeHeading+yawOffset)*180/Math.PI+360)%360;
    el('surfaceheading').textContent=`${Math.round(degrees)}° · CÁMARA A 1.70 M · RELIEVE ×1`;
    status(`${terrain.site==='synth'?'DEMO SINTÉTICA':terrain.site==='mola-global'?'MOLA · CONTEXTO REGIONAL ~3.7 KM':textureReady?(terrain.site==='glen-torridon'?'GLEN TORRIDON · ORTOFOTO HiRISE':'JEZERO · ORTOFOTO HiRISE'):'DEM · COLOR ILUSTRATIVO'} · Reconstrucción 3D, sin panoramas de superficie`);
  }
  function setOpen(value){open=value;el('surfaceview').hidden=!open;el('surfaceopen').setAttribute('aria-pressed',String(open));if(open){render();canvas.focus();}else {el('surfaceopen').focus();root.dispatchEvent(new CustomEvent('surface-close'));}}
  el('surfaceopen').onclick=()=>setOpen(true);el('surfaceclose').onclick=()=>setOpen(false);
  el('surfacerecenter').onclick=()=>{yawOffset=0;pitch=0;fov=75;render();};
  function step(direction){root.dispatchEvent(new CustomEvent('surface-step',{detail:direction*5}));}
  el('surfaceforward').onclick=()=>step(1);el('surfaceback').onclick=()=>step(-1);
  canvas.addEventListener('pointerdown',event=>{drag={x:event.clientX,y:event.clientY};canvas.setPointerCapture(event.pointerId);});
  canvas.addEventListener('pointermove',event=>{if(!drag)return;yawOffset-=(event.clientX-drag.x)*0.005;pitch=Math.max(-1.2,Math.min(1.2,pitch+(event.clientY-drag.y)*0.004));drag={x:event.clientX,y:event.clientY};render();});
  for(const name of ['pointerup','pointercancel'])canvas.addEventListener(name,()=>{drag=null;});
  canvas.addEventListener('wheel',event=>{event.preventDefault();fov=Math.max(40,Math.min(100,fov+event.deltaY*0.025));render();},{passive:false});
  canvas.addEventListener('keydown',event=>{
    if(event.key==='Escape'){setOpen(false);return;}
    const actions={ArrowLeft:()=>yawOffset-=0.1,ArrowRight:()=>yawOffset+=0.1,ArrowUp:()=>step(1),ArrowDown:()=>step(-1)};
    if(actions[event.key]){event.preventDefault();actions[event.key]();render();}
  });
  canvas.addEventListener('webglcontextlost',event=>{event.preventDefault();lost=true;status('Se perdió el contexto gráfico. Cierra la vista y recarga para recuperarlo.');});
  root.addEventListener('resize',render);
  root.SurfaceView={isOpen:()=>open,update(value){latest=value;
    const region=value.terrain?.region||value.terrain?.site;
    if(region && region!==referenceRegion){referenceRegion=region;reference=null;textureReady=false;textureRequested=false;const request=++referenceRequest;
      fetch(typeof apiURL==='function'?apiURL('/reference-metadata'):'/reference-metadata').then(response=>response.ok?response.json():null).then(meta=>{if(request!==referenceRequest)return;reference=meta;textureRequested=false;render();}).catch(()=>{});
    }el('surfaceopen').disabled=!value.sample || !value.terrain || lost;if(!value.sample && open)setOpen(false);render();}};
})(typeof window==='object'?window:globalThis);
