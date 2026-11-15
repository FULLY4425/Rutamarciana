"""Optional integration check. Start Python server and Mars relay first; see docs/RELOJ_MARS_OS.md."""
import urllib.request,json,os
from pathlib import Path
base=os.environ.get('RUTA_RELAY_URL','http://127.0.0.1:8790')
def call(path,payload=None):
 r=urllib.request.urlopen(urllib.request.Request(base+path,data=None if payload is None else json.dumps(payload).encode(),headers={'Content-Type':'application/json'}),timeout=45)
 return r.status,r.read()
m=json.loads((Path(__file__).resolve().parents[1]/'assets/glen-torridon/routes.json').read_text());s=m['spatial']
p=dict(stops=[dict(name=n,type='custom',lat=s['north_deg']-r*s['degree_step'],lon=s['west_deg']+c*s['degree_step']) for n,r,c in [('A',30,30),('B',30,40)]],maxSlopeDegrees=40,weather='clear')
status,b=call('/mr/plan',p);d=json.loads(b);assert status==200 and len(d['stops'])==2 and d['routed'],d
status,b=call('/api/psion/missions',p);d=json.loads(b);assert status==200 and len(d['code'])==4,d
code=d['code'];assert json.loads(call('/api/psion/missions/'+code)[1])['code']==code
status,b=call('/mr/weather?key=dust',{});assert json.loads(b)['speedKmH']==1.6
for mode in ['terrain','follow','surface','globe']:
 status,pixels=call('/mr/view3d?w=320&h=190&mode='+mode)
 assert status==200 and len(pixels)==320*190*2,(mode,status,len(pixels))
 assert len(set(zip(pixels[::2],pixels[1::2])))>20,mode+' image is blank'
 print('RGB565',mode,len(pixels),'bytes')
print('PASS: supplied relay, real Chromium, plan sync, weather, mission proxy and four 320x190 images')
