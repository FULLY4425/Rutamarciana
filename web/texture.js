/* Affine image mapping for a projected terrain triangle. */
(function(root){
 function affine(source,target){
  const [a,b,c]=source,[p,q,r]=target,dx=b[0]-a[0],dy=b[1]-a[1],ex=c[0]-a[0],ey=c[1]-a[1],det=dx*ey-dy*ex;
  if(Math.abs(det)<1e-10)return null;
  const A=((q[0]-p[0])*ey-(r[0]-p[0])*dy)/det,C=(dx*(r[0]-p[0])-ex*(q[0]-p[0]))/det;
  const B=((q[1]-p[1])*ey-(r[1]-p[1])*dy)/det,D=(dx*(r[1]-p[1])-ex*(q[1]-p[1]))/det;
  return [A,B,C,D,p[0]-A*a[0]-C*a[1],p[1]-B*a[0]-D*a[1]];
 }
 function paint(ctx,image,triangle,terrain){
  if(!image.complete||!image.naturalWidth)return false;
  const source=triangle.ids.map(([r,c])=>[terrain.cols[c]/(terrain.grid.cols-1)*image.naturalWidth,terrain.rows[r]/(terrain.grid.rows-1)*image.naturalHeight]);
  const transform=affine(source,triangle.pts);if(!transform)return false;
  ctx.save();ctx.beginPath();triangle.pts.forEach((p,i)=>i?ctx.lineTo(p[0],p[1]):ctx.moveTo(p[0],p[1]));ctx.closePath();ctx.clip();ctx.transform(...transform);ctx.drawImage(image,0,0);ctx.restore();return true;
 }
 if(typeof module==='object'&&module.exports)module.exports={affine};else root.TerrainTexture={paint};
})(typeof window==='object'?window:globalThis);
