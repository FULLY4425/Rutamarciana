"""Exercise the actual TFT renderer with pixel/text bounds and long wire messages."""
from pathlib import Path
import subprocess


def test_tft_320_240_layout_and_long_instructions(tmp_path):
    root=Path(__file__).resolve().parents[1]
    source=tmp_path/'tft.cpp'
    source.write_text(r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
struct Text {int x,y,size;std::string value;};
std::vector<Text> texts;
void bounds(int x,int y,int w,int h){assert(x>=0&&x+w<=320);assert(y>=18&&y+h<=214);}
struct Display {
 void fillRect(int x,int y,int w,int h,uint16_t){bounds(x,y,w,h);}
 void fillTriangle(int a,int b,int c,int d,int e,int f,uint16_t){bounds(a,b,1,1);bounds(c,d,1,1);bounds(e,f,1,1);}
 void drawCircle(int x,int y,int r,uint16_t){bounds(x-r,y-r,r*2+1,r*2+1);}
 void fillCircle(int x,int y,int r,uint16_t c){drawCircle(x,y,r,c);}
} tft;
const uint16_t C_INK=1,C_LCD=0,C_MID=2;
void putText(int x,int y,int size,uint16_t,uint16_t,const char* text){bounds(x,y,strlen(text)*6*size,8*size);texts.push_back({x,y,size,text});}
#include "navigator_tft.h"
int main(){
 ruta::Snapshot s;strcpy(s.mode,"training");s.stops=18;
 memset(s.instruction,'A',180);s.instruction[180]=0;
 const char* kinds[]={"left","right","uturn","home","science","destination","depart","straight","wait"};
 std::string note(63,'N');
 for(const auto kind:kinds)for(bool active:{true,false})for(float meters:{0.f,999.f,1000.f,99999.f,100000000.f})for(int action=0;action<3;action++){
  texts.clear();strcpy(s.kind,kind);s.distance_m=s.remaining_m=meters;
  ruta_tft::render(s,active,note.c_str(),action,3);
  bool bigDistance=false;std::string instruction;
  for(const auto& t:texts){if(active&&t.x==104&&t.y==62){assert(t.size==3);bigDistance=true;}if(t.y>=136&&t.y<=166)instruction+=t.value;}
  assert(!active||bigDistance);if(active)assert(instruction==s.instruction);
 }
 // A full-width long message is split safely, not passed to the shell's 24-byte wrapper.
 s.stops=0;texts.clear();ruta_tft::render(s,true,note.c_str(),0,3);
 for(const auto& t:texts)if(t.y==177){assert(t.value.size()==50);assert(t.value.substr(47)=="...");}
 strcpy(s.instruction,"Rumbo 90 E. En veinte metros gira a la derecha siguiendo la ruta marcada y continua hasta la siguiente parada cientifica. Luego inicia el regreso al origen.");
 texts.clear();ruta_tft::render(s,true,"Conectado",0,3);
 assert(ruta_tft::WIDTH==320&&ruta_tft::HEIGHT==240);
}
''')
    exe=tmp_path/'tft'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-I',str(root/'firmware/psion-micor-spaceapps'),str(source),'-o',str(exe)],check=True,capture_output=True)
    subprocess.run([str(exe)],check=True,capture_output=True)
