#pragma once
// Landscape 320x240 navigation content. Keep the shell title and dock intact.
#include "navigator_core.h"
#include <stdio.h>
namespace ruta_tft {
constexpr int WIDTH=320,HEIGHT=240,TOP=18,BOTTOM=214,MARGIN=8,COLUMNS=50;
inline const char* maneuver(const char* kind){
 return !strcmp(kind,"left")?"IZQUIERDA":!strcmp(kind,"right")?"DERECHA":!strcmp(kind,"uturn")?"DAR VUELTA":!strcmp(kind,"home")?"REGRESO":!strcmp(kind,"science")?"CIENCIA":!strcmp(kind,"destination")?"DESTINO":"SEGUIR";
}
inline void distance(float meters,char* out,size_t size){
 if(meters<1000)snprintf(out,size,"%.0f m",meters);
 else if(meters<100000)snprintf(out,size,"%.1f km",meters/1000);
 else snprintf(out,size,"%.0f km",meters/1000);
}
// The shell's wrapText has a 24-byte line buffer; do not pass TFT-width text to it.
inline void textLine(int y,const char* text,uint16_t color){
 char line[COLUMNS+1];size_t n=strlen(text);if(n>COLUMNS){memcpy(line,text,COLUMNS-3);memcpy(line+COLUMNS-3,"...",4);}else{memcpy(line,text,n+1);}
 putText(MARGIN,y,1,color,C_LCD,line);
}
inline void instruction(const char* text,uint16_t color){
 for(int row=0;row<4&&*text;row++){
  while(*text==' ')text++;
  const size_t remaining=strlen(text);size_t n=remaining<COLUMNS?remaining:COLUMNS;
  if(remaining>n&&text[n]!=' '){size_t cut=n;while(cut&&text[cut]!=' ')cut--;if(cut)n=cut;}
  char line[COLUMNS+1];memcpy(line,text,n);line[n]=0;
  if(row==3&&remaining>n){size_t end=n<COLUMNS-3?n:COLUMNS-3;memcpy(line+end,"...",4);}
  putText(MARGIN,136+row*10,1,color,C_LCD,line);text+=n;
 }
}
inline void icon(const char* kind,bool active,uint16_t color){
 const int x=12,y=61;
 if(!active){tft.fillRect(x+23,y+18,9,42,color);tft.fillRect(x+43,y+18,9,42,color);}
 else if(!strcmp(kind,"left")||!strcmp(kind,"right")){
  const bool left=!strcmp(kind,"left");tft.fillRect(x+(left?42:20),y+28,10,38,color);tft.fillRect(x+20,y+28,32,10,color);
  tft.fillTriangle(x+(left?20:52),y+17,x+(left?4:68),y+33,x+(left?20:52),y+49,color);
 }else if(!strcmp(kind,"uturn")){
  tft.fillRect(x+14,y+20,10,46,color);tft.fillRect(x+14,y+20,40,10,color);tft.fillRect(x+44,y+20,10,20,color);tft.fillTriangle(x+36,y+39,x+62,y+39,x+49,y+58,color);
 }else if(!strcmp(kind,"home")){
  tft.fillTriangle(x+12,y+34,x+36,y+12,x+60,y+34,color);tft.fillRect(x+20,y+34,32,30,color);tft.fillRect(x+30,y+46,12,18,C_LCD);
 }else if(!strcmp(kind,"science")){
  tft.fillRect(x+20,y+14,4,50,color);tft.fillTriangle(x+24,y+14,x+60,y+26,x+24,y+38,color);
 }else if(!strcmp(kind,"destination")){
  tft.drawCircle(x+36,y+36,22,color);tft.fillCircle(x+36,y+36,7,color);
 }else{tft.fillRect(x+31,y+28,10,38,color);tft.fillTriangle(x+18,y+28,x+36,y+8,x+54,y+28,color);}
}
inline void render(const ruta::Snapshot& s,bool active,const char* linkNote,int action,uint16_t accent){
 tft.fillRect(0,TOP,WIDTH,BOTTOM-TOP,C_LCD);
 putText(MARGIN,24,2,C_INK,C_LCD,"Navegador");
 textLine(45,!strcmp(s.mode,"training")?"GPS CELULAR > MARTE 1:1":"SIMULACION - SIN GPS",C_MID);
 icon(s.kind,active,accent);
 char value[24];distance(s.distance_m,value,sizeof value);
 putText(104,62,active?3:2,C_INK,C_LCD,active?value:"EN ESPERA");
 putText(104,92,2,accent,C_LCD,active?maneuver(s.kind):"REVISA GPS");
 char remaining[24],summary[36];distance(s.remaining_m,remaining,sizeof remaining);snprintf(summary,sizeof summary,"Restan %s",remaining);
 putText(104,115,1,C_MID,C_LCD,active?summary:"Sin guia vigente");
 instruction(active?s.instruction:"Detente y revisa el GPS, la pagina del celular y el enlace WiFi.",C_INK);
 textLine(177,s.stops>8?"EVA manual: la ruta supera 8 paradas":linkNote,C_MID);
 const char* actions[]={"Vincular celular","Desconectar","Volver a presupuesto"};
 char option[40];snprintf(option,sizeof option,"< %s >",actions[action>=0&&action<3?action:0]);textLine(190,option,accent);
 textLine(203,"Girar: opcion / pulsar: elegir",C_MID);
}
}
