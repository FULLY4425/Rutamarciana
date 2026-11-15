from pathlib import Path
import subprocess


def test_real_wire_parser_and_freshness_on_host(tmp_path):
    root=Path(__file__).resolve().parents[1]
    source=tmp_path/'navigator.cpp'
    source.write_text(r'''
#include "navigator_core.h"
#include <cassert>
#include <string>
int main(){
 ruta::Packet packet;uint8_t part[400]={0};assert(packet.append(part,400)==400);assert(packet.append(part,400)==0&&packet.overflow&&packet.size==400);
 ruta::Snapshot s;std::string wire="RUTA1\n12\ntraining\nlive\nright\n20\n100\n8\n1000\nGira a la derecha\n100\n110\n120\n125\n2\n60\n";
 assert(ruta::parse(wire.c_str(),wire.size(),s));assert(s.seq==12&&s.stops==2&&s.budgetValid());
 assert(s.active(5000,4000));assert(!s.active(12000,4000));
 s.age_ms=16000;assert(!s.active(5000,4000));s.age_ms=14000;assert(s.active(5000,4000));assert(!s.active(5001,4000));s.age_ms=0;
 assert(s.active(20,UINT32_MAX-20)); // millis rollover
 s.stops=18;assert(!s.budgetValid()); // no silent truncation of the existing EVA contract
 auto old=s;assert(!ruta::parse("bad",3,s));assert(s.stops==old.stops);
 std::string bad=wire+"EXTRA\n";assert(!ruta::parse(bad.c_str(),bad.size(),s));
 bad=wire;bad.replace(bad.find("\n20\n"),4,"\nnan\n");assert(!ruta::parse(bad.c_str(),bad.size(),s));
 assert(ruta::pairing("0123456789abcdef.0123456789abcdef0123456789abcdef"));
 assert(!ruta::pairing("0123456789abcdef.0123456789abcdef0123456789abcde?"));
}
''')
    exe=tmp_path/'navigator'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(root/'firmware/psion-micor-spaceapps'),str(source),'-o',str(exe)],check=True,capture_output=True)
    subprocess.run([str(exe)],check=True,capture_output=True)


def test_uploaded_firmware_unrelated_files_are_byte_identical():
    import hashlib,json
    root=Path(__file__).resolve().parents[1]/'firmware/psion-micor-spaceapps'
    manifest=json.loads((root/'upstream-sha256.json').read_text())
    for name,digest in manifest['files_before_navigation_changes'].items():
        if name!='spaceapps_apps.h':
            assert hashlib.sha256((root/name).read_bytes()).hexdigest()==digest
