#pragma once
// Small, pure viewport geometry helpers shared by the TFT renderer and host tests.
#include <cmath>
namespace pov_render {
inline int bound(int value,int low,int high){return value<low?low:value>high?high:value;}
inline bool clipLine(int& x0,int& y0,int& x1,int& y1,int width,int height){
 if(width<1||height<1)return false;
 const float dx=(float)x1-x0,dy=(float)y1-y0;float first=0,last=1;
 const float p[4]={-dx,dx,-dy,dy},q[4]={(float)x0,(float)(width-1-x0),(float)y0,(float)(height-1-y0)};
 for(int i=0;i<4;i++){
  if(p[i]==0){if(q[i]<0)return false;continue;}
  const float t=q[i]/p[i];
  if(p[i]<0){if(t>last)return false;if(t>first)first=t;}else{if(t<first)return false;if(t<last)last=t;}
 }
 const int sx=x0,sy=y0;
 x0=bound((int)lroundf(sx+first*dx),0,width-1);y0=bound((int)lroundf(sy+first*dy),0,height-1);
 x1=bound((int)lroundf(sx+last*dx),0,width-1);y1=bound((int)lroundf(sy+last*dy),0,height-1);return true;
}
}
