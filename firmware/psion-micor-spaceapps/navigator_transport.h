#pragma once
#include "navigator_core.h"
#include "navigator_ca.h"
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
}
