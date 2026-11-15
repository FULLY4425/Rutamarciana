"""Compile the real bitacora storage/JSON functions with a tiny host LittleFS."""
import os
from pathlib import Path
import subprocess
import tempfile


def test_bitacora_identity_host():
    ino = (Path(__file__).resolve().parents[1] / 'firmware/psion_micor_stable/bitacora.ino').read_text()
    storage = ino[ino.index('void btPath('):ino.index('void btFallback()')]
    body = ino[ino.index('int btBodyStart()'):ino.index('String btCsvCell(')]
    harness = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#define PSTR(x) x
#define snprintf_P snprintf
struct String : std::string {
  using std::string::string;
  String(const std::string &s):std::string(s) {}
  String(unsigned long n):std::string(std::to_string(n)) {}
  String(int n):std::string(std::to_string(n)) {}
  int indexOf(char c, int p=0) const { auto x=find(c,p); return x==npos?-1:(int)x; }
  int indexOf(const char *s, int p=0) const { auto x=find(s,p); return x==npos?-1:(int)x; }
  int indexOf(const String &s, int p=0) const { auto x=find(s,p); return x==npos?-1:(int)x; }
  String substring(unsigned a, unsigned b) const { if (b>size()) b=size(); return substr(a,b-a); }
  bool startsWith(const char *s) const { return find(s)==0; }
  void setCharAt(int p,char c) { (*this)[p]=c; }
};
struct FS;
struct File {
  FS *fs=nullptr; std::string path; bool valid=false;
  File(FS *f, const char *p, bool ok):fs(f),path(p),valid(ok) {}
  explicit operator bool() const { return valid; }
  size_t size(); String readString(); size_t write(const uint8_t *, size_t);
  void close() {} void flush() {}
};
struct FS {
  std::map<std::string,std::string> files; bool failWrite=false, failRename=false;
  bool exists(const char *p) { return files.count(p); }
  File open(const char *p, const char *mode) { if (*mode=='w') { files[p]=""; return {this,p,true}; } return {this,p,exists(p)}; }
  File open(const String &p, const char *mode) { return open(p.c_str(),mode); }
  bool remove(const String &p) { return files.erase(p)>0; }
  bool rename(const String &from,const char *to) { if(failRename) return false; files[to]=files[from]; files.erase(from); return true; }
} LittleFS;
size_t File::size() { return fs->files[path].size(); }
String File::readString() { return fs->files[path]; }
size_t File::write(const uint8_t *p,size_t n) { if(fs->failWrite) return 0; fs->files[path]=std::string((const char*)p,n); return n; }
struct { uint32_t chip=42; uint32_t getChipId() { return chip; } } ESP;
bool fsOk=true; int btId=0, btCount=0, btIds[12]; const int BT_LIMIT=3000, BT_MAX=12; String btData;
''' + storage + body + r'''
int main() {
  String first, second, third;
  assert(btNextIdentity(first)); assert(LittleFS.files["/btseq.txt"]=="1");
  btData=String("fecha=2026-11-14\nsync=0\ntitulo=Caminata 0\nid=")+first+"\n";
  assert(btSave()); assert(btLoad(0)); assert(btIdentity()==first); assert(btEntries()==0);
  btData += "12:00:00|parada=START|tipo=ROCA|accion=FOTO|estado=NORMAL|otros=x\n";
  assert(btSave()); assert(btLoad(0)); assert(btEntries()==1);
  String json=btJson(); assert(json.find(String("\"id\":\"")+first+"\"")!=String::npos);
  assert(btLoad(0)); assert(btJson()==json); // retry keeps identity
  assert(LittleFS.remove("/b0.txt"));
  assert(btNextIdentity(second)); assert(first!=second); // same local slot after delete
  btData=String("fecha=2026-11-14\nsync=0\ntitulo=Caminata 0\nid=")+second+"\n";
  assert(btSave()); assert(btLoad(0)); assert(btIdentity()==second);
  ESP.chip=43; LittleFS.files["/btseq.txt"]="1"; assert(btNextIdentity(third)); assert(third!=second);
  LittleFS.files["/btseq.txt"]="3";
  LittleFS.failRename=true; assert(!btNextIdentity(third)); assert(LittleFS.files["/btseq.txt"]=="3");
  LittleFS.failRename=false; LittleFS.failWrite=true; assert(!btNextIdentity(third)); assert(LittleFS.files["/btseq.txt"]=="3");
  LittleFS.failWrite=false; LittleFS.files["/btseq.txt"]="4294967295"; assert(!btNextIdentity(third));
  LittleFS.files["/btseq.txt"]="bad"; assert(!btNextIdentity(third));
  btData="fecha=2026-11-14\nsync=0\ntitulo=Caminata 0\n"; assert(btSave()); assert(btLoad(0)); assert(btIdentity()=="0");
  btData="fecha=2026-11-14\nsync=0\ntitulo=Caminata 0\nid=invalid\n"; assert(btSave()); assert(!btLoad(0));
  for (int i=0; i<BT_MAX; ++i) { char p[20]; btPath(p,sizeof p,i); LittleFS.files[p]="x"; }
  btScan(); assert(btCount==BT_MAX); assert(LittleFS.remove("/b5.txt")); btScan(); assert(btCount==BT_MAX-1);
}
'''
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as d:
        src = Path(d) / 'test.cpp'
        exe = Path(d) / 'test'
        src.write_text(harness)
        subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-o', str(exe), str(src)], check=True)
        subprocess.run([str(exe)], check=True)
