import json
from pathlib import Path
import subprocess
import numpy as np
import pytest
from relay.app import create_app
from relay.psion import GLEN, normalize, register_psion
from flask import Flask, jsonify

ROOT=Path(__file__).resolve().parents[1]


def mission():
    m=json.loads((GLEN/'routes.json').read_text());s=m['spatial']
    return dict(stops=[dict(name=name,type='custom',lat=s['north_deg']-row*s['degree_step'],lon=s['west_deg']+col*s['degree_step']) for name,row,col in [('Salida',30,30),('Destino',30,40)]],maxSlopeDegrees=40)


def test_psion_uses_glen_planner_even_with_jezero_default():
    app=create_app(str(ROOT/'assets/jezero'));client=app.test_client()
    r=client.post('/api/psion/missions',json=mission());assert r.status_code==200,r.json
    data=r.json;assert data['region']=='glen-torridon' and data['cells']>1
    assert data['planner']=='Ruta Marciana FAST (Python)'
    assert len(data['code'])==4 and len(data['pathCrc'])==8
    assert data['stops'][0]['x']==30 and data['stops'][-1]['x']==40
    assert client.get('/api/psion/missions/'+data['code']).json==data
    body=dict(start=[30,30],end=[30,40],max_slope_deg=40)
    expected=client.post('/api/route?region=glen-torridon',json=body).json['fast']['outbound']
    assert data['distance']==expected['distance_m']
    assert client.get('/api/health').json['project']=='Ruta Marciana'
    assert 'no-store' in r.headers['Cache-Control']
    for asset in ['psion','map-touch','mobile']:
        assert client.get('/field/'+asset+'.js').status_code==200


@pytest.mark.parametrize('edit',[
    {'region':'jezero'},{'maxSlopeDegrees':True},{'maxSlopeDegrees':41},
    {'stops':['unknown','Stornoway']},{'stops':['Stornoway']},{'stops':['Stornoway']*11},
    {'environment':{'weather':[]}},{'environment':{'weather':'unknown'}},{'environment':{'blocked_zones':[{'center':[999,999],'radius_m':30}]}}
])
def test_invalid_missions_are_rejected(edit):
    client=create_app(str(ROOT/'assets/jezero')).test_client()
    assert client.post('/api/psion/missions',json={**mission(),**edit}).status_code==400


def test_codes_expire_without_affecting_other_apis():
    app=Flask(__name__);now=[0]
    def planner(body, directory):
        return jsonify(fast={'outbound':{'points':[body['start'],body['end']],'distance_m':1,'time_s':2,'legs':[{'ascent_m':0}]}},environment={'weather':'clear','speed_factor':1,'blocked_zones':[]},spatial=json.loads((GLEN/'routes.json').read_text())['spatial'])
    register_psion(app,planner,lambda:now[0]);c=app.test_client()
    code=c.post('/api/psion/missions',json=mission()).json['code']
    now[0]=86401;assert c.get('/api/psion/missions/'+code).status_code==404
    assert c.get('/api/psion/missions/../invalid').status_code==404


def test_watch_map_and_core_against_repository_assets(tmp_path):
    src=tmp_path/'watch.cpp';exe=tmp_path/'watch'
    src.write_text(r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <cassert>
#define PROGMEM
#include "marte_core.h"
int main(){
 MrMem mem{};mrM=&mem;mrUnpack();mrEnvClear();mrSetLimit(40);
 for(int i=0;i<MR_N;i++)printf("%d\n",MR_BASE_CM+mrH(i));
 assert(MR_NSITES==136&&MR_NTARGETS==718);
 auto& r=mrM->r;r.lim=40;r.nStops=2;r.stop[0]=30*MR_W+30;r.stop[1]=30*MR_W+40;
 mrNameStop(0);mrNameStop(1);int bad=0;assert(mrSegments(1,&bad)==1);mrFinish();assert(r.ok&&r.len>1);
 for(int i=1;i<r.len;i++){int a=r.path[i-1],b=r.path[i];assert(abs(a%MR_W-b%MR_W)<=1&&abs(a/MR_W-b/MR_W)<=1);assert(mrOk(b));}
 char body[3200];assert(mrRequestJson(body,sizeof body)>0);assert(strstr(body,"environment"));
 char tiny[8];assert(mrRequestJson(tiny,sizeof tiny)==0);
 MrReply rp{};const char* valid="{\"stops\":[{\"name\":\"A\",\"x\":30,\"y\":30},{\"name\":\"B\",\"x\":40,\"y\":30}],\"maxSlopeDegrees\":40}";
 assert(mrParseReply(valid,&rp)&&rp.nStops==2);assert(!mrParseReply("{\"stops\":[{\"x\":999,\"y\":0}]}",&rp));
 MrEnv env{};assert(mrParseEnv("{\"weather\":\"dust\",\"speedKmH\":1.6,\"restrictions\":[]}",&env,nullptr,0));
 mrEnv=env;mrZoneCells();assert(mrEnv.speed==1.6);
 mrEnv.nZones=1;strcpy(mrEnv.zone[0].name,"hypothetical_dust");mrEnv.zone[0].lat=MR_NORTH-40*MR_STEP;mrEnv.zone[0].lon=MR_WEST+70*MR_STEP;mrEnv.zone[0].r=30;
 assert(mrRequestJson(body,sizeof body)>0);assert(strstr(body,"hypothetical_dust"));
}
''')
    subprocess.run(['g++','-std=c++17','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(ROOT/'firmware/psion-marte-os'),str(src),'-o',str(exe)],check=True,capture_output=True)
    result=subprocess.run([str(exe)],check=True,capture_output=True,text=True)
    actual=np.array([int(n) for n in result.stdout.splitlines()])
    expected=np.rint(np.load(GLEN/'dem.npy').astype(float).ravel()*100).astype(int)
    np.testing.assert_array_equal(actual,expected)


def test_watch_driver_and_font_preserved():
    import hashlib
    base=ROOT/'firmware/psion-marte-os';m=json.loads((base/'upstream-sha256.json').read_text())
    for name in ['font5x7.h','oled240.h']:
        assert hashlib.sha256((base/name).read_bytes()).hexdigest()==m['files'][name]


def test_exclusions_survive_public_code_and_body_limit():
    client=create_app(str(ROOT/'assets/jezero')).test_client()
    environment={'weather':'dust','blocked_zones':[{'center':[40,70],'radius_m':30,'kind':'manual_obstacle'}]}
    r=client.post('/api/psion/missions',json={**mission(),'environment':environment});assert r.status_code==200
    data=client.get('/api/psion/missions/'+r.json['code']).json
    assert data['environment']['blocked_zones']==environment['blocked_zones']
    assert data['speedKmH']==1.6 and data['restrictions'][0]['radiusMeters']==30
    assert client.post('/api/psion/missions',data=' '*12001,content_type='application/json').status_code==413


def test_mailbox_capacity_is_bounded(monkeypatch):
    monkeypatch.setattr('relay.psion.CAPACITY',1)
    client=create_app(str(ROOT/'assets/jezero')).test_client()
    assert client.post('/api/psion/missions',json=mission()).status_code==200
    assert client.post('/api/psion/missions',json=mission()).status_code==429
