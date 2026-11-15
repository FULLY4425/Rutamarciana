#pragma once
// Included after the shell helpers. Networking stays off the UI task.
#include "navigator_core.h"
#include "navigator_ca.h"
#include "navigator_tft.h"
static_assert(W==ruta_tft::WIDTH && H==ruta_tft::HEIGHT,"Navegador TFT requiere 320x240 horizontal");
#include "navigator_transport.h"
namespace ruta_link {
static int action=0;
static bool editing=false;
inline bool provide(mision::RouteInput* out,int variant){
 if(!out||(variant!=mision::FAST&&variant!=mision::SAFE))return false;
 uint32_t at;ruta::Snapshot s=snapshot(&at);
 if((uint32_t)(millis()-at)>6000||!strcmp(s.status,"stale")||!s.budgetValid())return false; // Never truncate the existing 8-stop EVA contract.
 *out=mision::RouteInput();out->valid=true;out->variant=variant;
 out->travel_out_s=variant==mision::FAST?s.fast_out_s:s.safe_out_s;out->travel_back_s=variant==mision::FAST?s.fast_back_s:s.safe_back_s;
 out->n_stops=s.stops;for(int i=0;i<s.stops;i++)out->dwell_s[i]=s.dwell_s;return true;
}
inline const char* turnName(const char* kind){
 return !strcmp(kind,"left")?"<< IZQUIERDA":!strcmp(kind,"right")?"DERECHA >>":!strcmp(kind,"uturn")?"DAR VUELTA":!strcmp(kind,"home")?"REGRESO":!strcmp(kind,"science")?"CIENCIA":!strcmp(kind,"destination")?"DESTINO":"SEGUIR";
}
inline void draw(){
 uint32_t at;ruta::Snapshot s=snapshot(&at);char linkNote[64];
 portENTER_CRITICAL(&guard);memcpy(linkNote,note,sizeof linkNote);portEXIT_CRITICAL(&guard);
 ruta_tft::render(s,s.active(millis(),at),linkNote,action,msAccent());oledDirty=true;
}
inline void tick(int clicks,bool pressed){
 if(editing){int result=askTick(clicks,pressed);if(!result)return;editing=false;if(result>0)connect(askBuf);draw();return;}
 if(clicks){action=((action+clicks)%3+3)%3;draw();}
 if(pressed){if(action==0){editing=true;askBegin(FS("Codigo del celular (ID.TOKEN)"),"",49,msAccent());}else if(action==1){disconnect();draw();}else{msPage=MS_PRESU;msDraw();}return;}
 static uint32_t painted=0;if((uint32_t)(millis()-painted)>=1000){painted=millis();draw();}
}
inline void oled(){
 uint32_t at;ruta::Snapshot s=snapshot(&at);char a[24],b[24],c[24];bool active=s.active(millis(),at);
 snprintf(a,sizeof a,"%s",active?turnName(s.kind):"EN ESPERA");snprintf(b,sizeof b,"En %.0f m",s.distance_m);snprintf(c,sizeof c,"restan %.0f m",s.remaining_m);
 oledCard(FS("RUTA MARCIANA"),a,active?b:"Revisa el celular",active?c:"Sin posicion reciente",!strcmp(s.mode,"training")?"GPS celular 1:1":"SIMULACION");
}
}
