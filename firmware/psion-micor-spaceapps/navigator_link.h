#pragma once
// Included after the shell helpers. Networking stays off the UI task.
#include "navigator_core.h"
#include "navigator_ca.h"
#include "navigator_tft.h"
static_assert(W==ruta_tft::WIDTH && H==ruta_tft::HEIGHT,"Navegador TFT requiere 320x240 horizontal");
#ifndef RUTA_NAV_SERVER
#define RUTA_NAV_SERVER "https://rutamarciana.onrender.com"
#endif
namespace ruta_link {
static portMUX_TYPE guard=portMUX_INITIALIZER_UNLOCKED;
static ruta::Snapshot current;
static uint32_t received=0,generation=0;
static char pairCode[50]="",note[64]="Vincula desde el celular";
static bool enabled=false;
static TaskHandle_t task=nullptr;
static int action=0;
static bool editing=false;
inline void message(const char* text){portENTER_CRITICAL(&guard);snprintf(note,sizeof note,"%s",text);portEXIT_CRITICAL(&guard);}
inline ruta::Snapshot snapshot(uint32_t* at=nullptr){portENTER_CRITICAL(&guard);ruta::Snapshot value=current;if(at)*at=received;portEXIT_CRITICAL(&guard);return value;}
inline void disconnect(){portENTER_CRITICAL(&guard);enabled=false;generation++;pairCode[0]=0;current=ruta::Snapshot();received=0;strcpy(note,"Desvinculado");portEXIT_CRITICAL(&guard);}
class PacketStream:public Stream {
public:
 ruta::Packet packet;
 int available() override{return 0;}
 int read() override{return -1;}
 int peek() override{return -1;}
 void flush() override{}
 size_t write(uint8_t byte) override{return packet.append(&byte,1);}
 size_t write(const uint8_t* bytes,size_t size) override{return packet.append(bytes,size);}
};
static void worker(void*){
 for(;;){
  char code[50];uint32_t revision;bool on;
  portENTER_CRITICAL(&guard);on=enabled;revision=generation;memcpy(code,pairCode,sizeof code);portEXIT_CRITICAL(&guard);
  if(!on){vTaskDelay(pdMS_TO_TICKS(200));continue;}
  if(WiFi.status()!=WL_CONNECTED){message("Sin WiFi - revisa hotspot");vTaskDelay(pdMS_TO_TICKS(2000));continue;}
  if(time(nullptr)<1700000000){configTime(0,0,"time.google.com","pool.ntp.org");message("Sincronizando hora para TLS");vTaskDelay(pdMS_TO_TICKS(2000));continue;}
  WiFiClientSecure client;client.setCACert(RUTA_ROOT_CA);client.setHandshakeTimeout(4);
  HTTPClient http;http.setConnectTimeout(2500);http.setTimeout(2500);http.useHTTP10(true);http.setReuse(false);
  char ident[17];memcpy(ident,code,16);ident[16]=0;
  String url=String(RUTA_NAV_SERVER)+"/api/navigator/sessions/"+ident+"?format=esp";
  bool ok=false;ruta::Snapshot value;
  if(http.begin(client,url)){
   http.addHeader("Authorization",String("Bearer ")+String(code+17));
   int status=http.GET();
   if(status==200){
    PacketStream sink;int n=http.writeToStream(&sink);
    ok=n>0&&!sink.packet.overflow&&ruta::parse(sink.packet.data,sink.packet.size,value);
    if(!ok)message("Respuesta de guia invalida");
   }else message(status==404?"Vinculo caducado: crea otro":status==403?"Codigo incorrecto":status<0?"Sin enlace HTTPS / TLS":"Sin respuesta valida");
   http.end();
  }else message("No se pudo abrir HTTPS");
  if(ok){portENTER_CRITICAL(&guard);if(enabled&&revision==generation){current=value;received=millis();strcpy(note,"Celular conectado por WiFi");}portEXIT_CRITICAL(&guard);}
  vTaskDelay(pdMS_TO_TICKS(2000));
 }
}
inline bool connect(const char* code){
 if(!ruta::pairing(code)){message("Codigo: 16 hex . 32 hex");return false;}
 if(!task&&xTaskCreate(worker,"ruta-nav",12288,nullptr,1,&task)!=pdPASS){task=nullptr;message("Sin RAM para el enlace");return false;}
 portENTER_CRITICAL(&guard);strcpy(pairCode,code);generation++;enabled=true;current=ruta::Snapshot();received=0;strcpy(note,"Conectando al celular...");portEXIT_CRITICAL(&guard);return true;
}
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
