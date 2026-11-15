/* Pointer gesture state: taps survive small finger jitter; pinch never places a point. */
(function(root){
 class Gesture {
  constructor(callbacks){this.callbacks=callbacks;this.points=new Map();this.blocked=false;}
  pair(){const p=[...this.points.values()];return {x:(p[0].x+p[1].x)/2,y:(p[0].y+p[1].y)/2,distance:Math.hypot(p[1].x-p[0].x,p[1].y-p[0].y)};}
  down(event){
   if(event.pointerType==='mouse'&&event.button!==0&&event.button!==1)return false;
   this.points.set(event.pointerId,{x:event.clientX,y:event.clientY,startX:event.clientX,startY:event.clientY,dragging:false,type:event.pointerType,pan:event.button===1});
   if(this.points.size>1)this.blocked=true;return true;
  }
  move(event){
   const point=this.points.get(event.pointerId);if(!point)return;
   const before=this.points.size===2?this.pair():null,dx=event.clientX-point.x,dy=event.clientY-point.y;
   point.x=event.clientX;point.y=event.clientY;
   if(before){const after=this.pair();this.callbacks.pinch?.({x:after.x,y:after.y,dx:after.x-before.x,dy:after.y-before.y,factor:before.distance>0?after.distance/before.distance:1});return;}
   if(this.points.size!==1||this.blocked)return;
   if(Math.hypot(point.x-point.startX,point.y-point.startY)>(point.type==='touch'?12:5))point.dragging=true;
   if(point.dragging)this.callbacks.drag?.({dx,dy,pan:point.pan||event.shiftKey,event});
  }
  up(event){
   const point=this.points.get(event.pointerId);if(!point)return;
   this.points.delete(event.pointerId);
   const threshold=point.type==='touch'?12:5;
   if(!this.blocked&&!point.dragging&&Math.hypot(event.clientX-point.startX,event.clientY-point.startY)<=threshold)this.callbacks.tap?.(event);
   if(!this.points.size)this.blocked=false;
  }
  cancel(){this.points.clear();this.blocked=false;}
 }
 if(typeof module==='object'&&module.exports)module.exports={Gesture};else root.MapGestures={Gesture};
})(typeof window==='object'?window:globalThis);
