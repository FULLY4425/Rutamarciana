#pragma once
// UI adapter for the supplied dual-ST7789 Mars OS. Network transport is shared.
#include "../psion-micor-spaceapps/navigator_transport.h"
static_assert(W==320 && H==240,"Ruta guide needs landscape 320x240");
namespace watch_guide {
static int action=0;
static uint32_t painted=0;
static char lastCue[181]="";
inline const char* maneuver(const char* kind){
 return !strcmp(kind,"left")?L("IZQUIERDA","LEFT"):!strcmp(kind,"right")?L("DERECHA","RIGHT"):!strcmp(kind,"uturn")?L("DAR VUELTA","TURN BACK"):!strcmp(kind,"home")?L("REGRESO","HOME"):!strcmp(kind,"science")?L("CIENCIA","SCIENCE"):!strcmp(kind,"destination")?L("DESTINO","DESTINATION"):L("SEGUIR","CONTINUE");
}
inline void draw(){
 uint32_t at;ruta::Snapshot s=ruta_link::snapshot(&at);const bool active=s.active(millis(),at);
 char note[64];portENTER_CRITICAL(&ruta_link::guard);memcpy(note,ruta_link::note,sizeof note);portEXIT_CRITICAL(&ruta_link::guard);
 tft.fillScreen(C_BG);drawTitle(L("Guia del celular","Phone guide"));
 putText(8,30,1,C_MUTED,C_BG,!strcmp(s.mode,"training")?"GPS CELULAR > MARTE 1:1":"SIMULACION / SIMULATION");
 char b[48];snprintf(b,sizeof b,"%.0f m",s.distance_m);
 putText(8,52,3,C_TEXT,C_BG,active?b:L("EN ESPERA","WAITING"));
 putText(8,84,2,active?C_ORANGE:C_YELLOW,C_BG,active?maneuver(s.kind):L("REVISA CELULAR","CHECK PHONE"));
 snprintf(b,sizeof b,L("Restan %.0f m","%.0f m remaining"),s.remaining_m);putText(8,108,1,C_TEXT,C_BG,active?b:note);
 putWrap(8,124,49,4,11,C_TEXT,C_BG,active?s.instruction:L("Sin posicion reciente. No sigas una indicacion antigua.","No fresh position. Do not follow old directions."));
 const char* options[3]={L("Vincular celular","Pair phone"),L("Desvincular","Unpair"),L("Volver al mapa","Back to map")};
 for(int i=0;i<3;i++){const int x=5+i*105;tft.fillRect(x,182,102,27,i==action?C_ORANGE:C_PANEL);putText(x+3,191,1,i==action?C_ONOR:C_TEXT,i==action?C_ORANGE:C_PANEL,options[i]);}
 drawHints(L("Perilla: elegir  OK: abrir  Mantener: inicio","Knob: choose  OK: open  Hold: home"));oledDirty=true;
 if(active&&s.distance_m<=10&&strcmp(lastCue,s.instruction)){
  snprintf(lastCue,sizeof lastCue,"%s",s.instruction);
  if(!strcmp(s.kind,"left"))PLAYP(T_LEFT,1);else if(!strcmp(s.kind,"right"))PLAYP(T_RIGHT,2);else PLAY(T_STOP);
 }
}
inline void small(){uint32_t at;ruta::Snapshot s=ruta_link::snapshot(&at);bool active=s.active(millis(),at);char a[24],b[24];snprintf(a,sizeof a,"%.0f m",s.distance_m);snprintf(b,sizeof b,"%.0f m",s.remaining_m);oledCard(L("GUIA CELULAR","PHONE GUIDE"),active?maneuver(s.kind):L("EN ESPERA","WAITING"),active?a:L("Revisa el celular","Check phone"),active?b:"",!strcmp(s.mode,"training")?"GPS 1:1":"SIMULACION");}
inline void tick(uint32_t now){
 int c=takeClicks(),pd=padTake();if(pd==PD_LEFT||pd==PD_UP)c--;else if(pd==PD_RIGHT||pd==PD_DOWN)c++;
 if(c){action=((action+c)%3+3)%3;draw();}
 if(takeShort()){
  if(action==0){kbBegin(5,L("Codigo celular: ID.TOKEN","Phone code: ID.TOKEN"),"",49,0);return;}
  if(action==1){ruta_link::disconnect();lastCue[0]=0;draw();}else{go(SC_MAP);return;}
 }
 if(now-painted>=1000){painted=now;draw();}
}
}
