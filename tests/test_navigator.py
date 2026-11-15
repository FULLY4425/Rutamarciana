import pytest
from flask import Flask
from relay.navigator import register_navigator


@pytest.fixture
def nav():
    now=[100.]
    app=Flask(__name__)
    register_navigator(app,clock=lambda:now[0])
    return app.test_client(),now


def paired(client):
    result=client.post('/api/navigator/sessions')
    assert result.status_code==201
    data=result.json
    path='/api/navigator/sessions/'+data['id']
    return path,{'Authorization':'Bearer '+data['read_token']},{'Authorization':'Bearer '+data['write_token']}


def message(**changes):
    return dict(mode='training',status='live',kind='right',instruction='Gira a la derecha.',distance_m=20,
                remaining_m=100,accuracy_m=8,fix_age_ms=1000,**changes)


def test_separate_credentials_no_coordinates_and_bounded_wire(nav):
    client,now=nav
    path,read,write=paired(client)
    assert client.get(path).status_code==403
    assert client.put(path,json=message(),headers=read).status_code==403
    assert client.get(path,headers=write).status_code==403
    assert client.put(path,json={**message(),'earth_latitude':52,'earth_longitude':13},headers=write).status_code==200
    got=client.get(path,headers=read)
    assert got.json['status']=='live'
    assert 'earth_latitude' not in got.json
    assert got.headers['Cache-Control']=='no-store'
    wire=client.get(path+'?format=esp',headers=read).text
    assert wire.startswith('RUTA1\n1\ntraining\nlive\nright\n20\n100\n8\n')
    assert len(wire.splitlines())==16 and len(wire)<768
    assert client.delete(path,headers=read).status_code==403
    assert client.delete(path,headers=write).status_code==200
    assert client.get(path,headers=read).status_code==404


def test_stale_phone_or_stale_fix_removes_turns(nav):
    client,now=nav;path,read,write=paired(client)
    client.put(path,json=message(),headers=write)
    now[0]+=15
    value=client.get(path,headers=read).json
    assert value['status']=='stale' and value['kind']=='wait' and value['distance_m']==0
    client.put(path,json={**message(),'fix_age_ms':16000},headers=write)
    assert client.get(path,headers=read).json['status']=='stale'
    now[0]+=1801
    assert client.get(path,headers=read).status_code==404


@pytest.mark.parametrize('patch',[{'distance_m':float('nan')},{'accuracy_m':-1},{'mode':'GPS_MARS'},
    {'instruction':'a'*181},{'budget':{'stops':19}},{'status':[]}])
def test_invalid_messages_do_not_replace_latest(nav,patch):
    client,now=nav;path,read,write=paired(client)
    assert client.put(path,json=message(),headers=write).status_code==200
    assert client.put(path,json={**message(),**patch},headers=write).status_code==400
    assert client.get(path,headers=read).json['seq']==1


def test_wire_cannot_inject_lines_and_preserves_18_stop_budget(nav):
    client,now=nav;path,read,write=paired(client)
    data={**message(),'instruction':'Gira\nizquierda — dirección','budget':dict(fast_out_s=100,fast_back_s=110,safe_out_s=120,safe_back_s=125,stops=18,dwell_s=60)}
    assert client.put(path,json=data,headers=write).status_code==200
    wire=client.get(path+'?format=esp',headers=read).text
    assert wire.isascii() and len(wire.splitlines())==16
    assert wire.splitlines()[-2]=='18'


def test_session_capacity_and_expiry(nav):
    client,now=nav
    for _ in range(64):assert client.post('/api/navigator/sessions').status_code==201
    assert client.post('/api/navigator/sessions').status_code==429
    now[0]+=1801
    assert client.post('/api/navigator/sessions').status_code==201


def test_navigator_script_and_view_are_served(nav):
    import server
    client=server.application().test_client()
    assert client.get('/field/navigator.js').status_code==200
    assert 'id="navigator"' in client.get('/').text


def test_imprecise_fix_is_not_an_active_turn(nav):
    client,now=nav;path,read,write=paired(client)
    assert client.put(path,json={**message(),'accuracy_m':50},headers=write).status_code==200
    value=client.get(path,headers=read).json
    assert value['status']=='waiting' and value['kind']=='wait'
