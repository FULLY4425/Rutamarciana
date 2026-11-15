"""Verify the received module against the merged firmware and its real tiles."""
import hashlib
import json
from pathlib import Path
import subprocess

import numpy as np
from pipeline import pov_tiles

ROOT=Path(__file__).resolve().parents[1]
FW=ROOT/'firmware/psion-micor-spaceapps'


def test_received_pov_core_checks(tmp_path):
    source=ROOT/'firmware/host_tests_spaceapps/test_pov_core.cpp'
    executable=tmp_path/'pov'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-O1','-fsanitize=address,undefined',str(source),'-o',str(executable)],check=True,capture_output=True)
    result=subprocess.run([str(executable)],check=True,capture_output=True,text=True)
    assert '109' in result.stdout


def test_received_tiles_match_the_existing_scientific_image():
    result=pov_tiles.verify(ROOT/'assets/jezero_ortho_4m.png',ROOT/'assets/jezero_ortho_4m.json',FW/'data/pov')
    assert result['roundtrip_exacto'] and result['tiles']==64
    assert all(result['max_err'][key]<=result['techo_cuantizado'][key] for key in 'rgb')


def test_main_sketch_has_only_the_received_rover_hooks_and_preserves_other_apps():
    manifest=json.loads((FW/'rover-upstream-sha256.json').read_text())
    assert hashlib.sha256((FW/'psion-micor-spaceapps.ino').read_bytes()).hexdigest()==manifest['files_received']['firmware/psion-micor-spaceapps/psion-micor-spaceapps.ino']
    shell=(FW/'spaceapps_apps.h').read_text()
    assert 'ruta_link::tick(c, sp)' in shell and 'mision::setMapProvider(ruta_link::provide)' in shell
    assert '#include "rover_app.h"' in shell
    for name in ('bitacora_core.h','eva_budget.h','mision_map_contract.h','forest.ino'):
        assert hashlib.sha256((FW/name).read_bytes()).hexdigest()==manifest['files_received']['firmware/psion-micor-spaceapps/'+name]


def test_real_row_blits_and_partial_tiles(tmp_path):
    # Compile actual imported UI functions against filesystem and display doubles.
    text=(FW/'rover_app.h').read_text()
    row=text[text.index('bool rvRowBlit('):text.index('// Empuja una fila')]
    blit=text[text.index('void rvBlitWindow('):text.index('// Blit de un rect')]
    source=tmp_path/'render.cpp'
    source.write_text(r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include "pov_core.h"
#define FS(x) x
#define PSTR(x) x
#define snprintf_P snprintf
struct File {
 std::ifstream f;
 File(){} explicit File(const std::string& p):f(p,std::ios::binary){}
 explicit operator bool()const{return f.is_open();}
 bool seek(uint32_t offset){f.clear();f.seekg(offset);return !!f;}
 size_t read(uint8_t* out,size_t n){f.read((char*)out,n);return f.gcount();}
 void close(){f.close();}
};
struct Filesystem {std::string base;bool missing=false;File open(const char* p,const char*){return missing?File():File(base+p);}} LittleFS;
bool fsOk=true,rvNoTiles=false;const uint16_t RV_NO_TILE=0x8410;
const int RV_WIN_Y=18;uint16_t rvSeg[64],canvas[200*160];
void rvPush(int x,int y,int w,uint16_t* pixels){assert(x>=0&&x+w<=200&&y>=18&&y<178);for(int i=0;i<w;i++)canvas[(y-18)*200+x+i]=pixels[i];}
''' + row + blit + r'''
int main(int argc,char**argv){
 assert(argc==3);LittleFS.base=argv[1];std::ifstream in(argv[2],std::ios::binary);std::vector<uint16_t> expected(500*500);in.read((char*)expected.data(),expected.size()*2);assert(in.gcount()==500*500*2);
 for(auto corner: {std::pair<int,int>{0,0},{17,19},{63,63},{289,309},{300,340}}){
  pov::Cam cam{corner.first,corner.second,200,160};rvBlitWindow(cam);assert(!rvNoTiles);
  for(int y=0;y<160;y++)for(int x=0;x<200;x++)assert(canvas[y*200+x]==expected[(cam.y+y)*500+cam.x+x]);
 }
 uint16_t row[64];assert(rvRowBlit(435,499,64,row));for(int x=0;x<64;x++)assert(row[x]==expected[499*500+435+x]);
 assert(!rvRowBlit(499,0,2,row));assert(!rvRowBlit(-1,0,10,row));assert(!rvRowBlit(0,500,10,row));
 LittleFS.missing=true;rvBlitWindow({17,19,200,160});assert(rvNoTiles);for(auto p:canvas)assert(p==RV_NO_TILE);
}
''')
    from PIL import Image
    image=np.asarray(Image.open(ROOT/'assets/jezero_ortho_4m.png').convert('RGB'),dtype=np.uint8)
    expected=tmp_path/'expected.bin';expected.write_bytes(pov_tiles.to_rgb565(image).astype('<u2').tobytes())
    executable=tmp_path/'render'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(FW),str(source),'-o',str(executable)],check=True,capture_output=True)
    subprocess.run([str(executable),str(FW/'data'),str(expected)],check=True,capture_output=True)


def test_rover_overlay_clips_to_its_own_viewport(tmp_path):
    source=tmp_path/'clip.cpp'
    source.write_text(r'''
#include <cassert>
#include "pov_render.h"
int main(){
 int a=-100,b=-100,c=300,d=200;assert(pov_render::clipLine(a,b,c,d,200,160));assert(a>=0&&a<200&&c>=0&&c<200&&b>=0&&b<160&&d>=0&&d<160);
 a=-10;b=-10;c=-5;d=-5;assert(!pov_render::clipLine(a,b,c,d,200,160));
 for(int x=-500;x<=500;x+=25)for(int y=-500;y<=500;y+=25){a=x;b=y;c=100;d=80;assert(pov_render::clipLine(a,b,c,d,200,160));assert(a>=0&&a<200&&b>=0&&b<160&&c>=0&&c<200&&d>=0&&d<160);}
}
''')
    executable=tmp_path/'clip'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(FW),str(source),'-o',str(executable)],check=True,capture_output=True)
    subprocess.run([str(executable)],check=True,capture_output=True)


def test_rover_text_is_bounded_in_panel_and_long_error_messages(tmp_path):
    text=(FW/'rover_app.h').read_text()
    helpers=text[text.index('void rvText('):text.index('void rvLine(')]
    source=tmp_path/'text.cpp'
    source.write_text(r'''
#include <cassert>
#include <cstring>
#include <string>
#include "pov_core.h"
const int RV_POV=0,RV_CARD=1,RV_WIN_W=200,W=320,TITLE_H=18,DOCK_Y=214;int rvPage=RV_POV;
pov::Route rvRoute;int lines=0;
void putText(int x,int y,int size,uint16_t,uint16_t,const char* text){
 int right=rvPage==RV_POV&&rvRoute.valid&&x<RV_WIN_W?200:316;
 assert(x>=0&&x+(int)strlen(text)*6*size<=right&&y>=18&&y+8*size<=214);lines++;
}
''' + helpers + r'''
int main(){
 std::string longText(500,'A');
 rvRoute.valid=true;rvText(204,23,1,0,0,longText.c_str());rvText(4,181,1,0,0,longText.c_str());
 rvRoute.valid=false;rvWrap(longText.c_str(),8,92,50,5,0,0);assert(lines==7);
 rvPage=RV_CARD;rvText(8,24,2,0,0,longText.c_str());
}
''')
    executable=tmp_path/'text'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(FW),str(source),'-o',str(executable)],check=True,capture_output=True)
    subprocess.run([str(executable)],check=True,capture_output=True)
