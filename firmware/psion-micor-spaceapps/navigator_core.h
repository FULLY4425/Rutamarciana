#ifndef RUTA_NAVIGATOR_CORE_H
#define RUTA_NAVIGATOR_CORE_H
// Bounded ASCII wire decoder. No Arduino, heap allocation, GPS driver or EVA changes.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
namespace ruta {
struct Packet {
 char data[768];size_t size=0;bool overflow=false;
 size_t append(const uint8_t* bytes,size_t count){
  if(overflow||count>sizeof(data)-1-size){overflow=true;return 0;}
  memcpy(data+size,bytes,count);size+=count;return count;
 }
};
struct Snapshot {
 uint32_t seq=0, age_ms=0;
 char mode[12]="training",status[16]="waiting",kind[16]="wait",instruction[181]="Esperando al celular";
 float distance_m=0,remaining_m=0,accuracy_m=0;
 float fast_out_s=-1,fast_back_s=-1,safe_out_s=-1,safe_back_s=-1,dwell_s=-1;
 int stops=-1;
 bool active(uint32_t now,uint32_t received)const {
  const uint32_t elapsed=(uint32_t)(now-received);
  return (!strcmp(status,"live")||!strcmp(status,"simulation")) && elapsed<=6000 && age_ms+elapsed<=15000 && (strcmp(mode,"training")||accuracy_m<=25);
 }
 bool budgetValid()const{return stops>=0&&stops<=8&&fast_out_s>=0&&fast_back_s>=0&&safe_out_s>=0&&safe_back_s>=0&&dwell_s>=0;}
};
inline bool oneOf(const char* value,const char* const* list,size_t n){for(size_t i=0;i<n;i++)if(!strcmp(value,list[i]))return true;return false;}
inline bool number(const char* s,float& out,float lo,float hi){char* end=nullptr;out=strtof(s,&end);return s[0]&&end&&!*end&&isfinite(out)&&out>=lo&&out<=hi;}
inline bool parse(const char* wire,size_t size,Snapshot& out){
 if(!wire||size==0||size>=768)return false;
 char buffer[768];memcpy(buffer,wire,size);buffer[size]=0;
 char* lines[16];char* p=buffer;
 for(int i=0;i<16;i++){lines[i]=p;char* end=strchr(p,'\n');if(!end)return false;*end=0;p=end+1;}
 if(*p||strcmp(lines[0],"RUTA1"))return false;
 Snapshot value;float n;
 if(!number(lines[1],n,0,1e8)||floorf(n)!=n)return false;
 value.seq=(uint32_t)n;
 const char* modes[]={"training","simulation"};const char* states[]={"live","simulation","waiting","paused","off_route","stale","outside"};
 const char* kinds[]={"depart","left","right","uturn","science","destination","home","straight","wait"};
 if(!oneOf(lines[2],modes,2)||!oneOf(lines[3],states,7)||!oneOf(lines[4],kinds,9))return false;
 strcpy(value.mode,lines[2]);strcpy(value.status,lines[3]);strcpy(value.kind,lines[4]);
 if(!number(lines[5],value.distance_m,0,1e8)||!number(lines[6],value.remaining_m,0,1e8)||!number(lines[7],value.accuracy_m,0,1e6)||!number(lines[8],n,0,1e9))return false;
 value.age_ms=(uint32_t)n;
 if(strlen(lines[9])>=sizeof value.instruction)return false;
 for(const char* c=lines[9];*c;c++)if((unsigned char)*c<32||(unsigned char)*c>126)return false;
 strcpy(value.instruction,lines[9]);
 if(!number(lines[10],value.fast_out_s,-1,1e8)||!number(lines[11],value.fast_back_s,-1,1e8)||!number(lines[12],value.safe_out_s,-1,1e8)||!number(lines[13],value.safe_back_s,-1,1e8)||!number(lines[14],n,-1,18)||floorf(n)!=n||!number(lines[15],value.dwell_s,-1,1e8))return false;
 value.stops=(int)n;out=value;return true;
}
inline bool pairing(const char* code){if(!code||strlen(code)!=49||code[16]!='.')return false;for(int i=0;i<49;i++)if(i!=16&&!((code[i]>='0'&&code[i]<='9')||(code[i]>='a'&&code[i]<='f')))return false;return true;}
}
#endif
