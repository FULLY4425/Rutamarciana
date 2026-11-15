import hashlib
import json
from pathlib import Path
import numpy as np
import pytest
import server
from pipeline.fetch_chemistry import records
from pipeline.restrictions import apply_environment
from pipeline.mission import mission_context

ROOT=Path(__file__).resolve().parents[1]


def test_official_chemical_observations_are_reproducible_and_exactly_geolocated():
    folder=ROOT/'assets/glen-torridon'
    data=json.loads((folder/'chemistry.json').read_text());targets=json.loads((folder/'targets.json').read_text())
    indexed={t['id']:t for t in targets};parsed=[]
    for category in ('veins','nodules'):
        raw=(folder/f'chemistry_{category}.csv').read_bytes()
        source=next(s for s in data['sources'] if s['url'].endswith(category+'.csv'))
        assert hashlib.sha256(raw).hexdigest()==source['sha256']
        rows,unmatched=records(raw,category,targets);parsed+=rows
        assert unmatched==source['unmatched_observations']
    assert parsed==data['observations'] and len(parsed)==878
    assert len({r['target_id'] for r in parsed})==320
    for row in parsed:
        target=indexed[row['target_id']]
        assert (row['target'],row['sequence'],row['lat'],row['lon'])==(target['name'],target['sequence'],target['lat'],target['lon'])
    first=parsed[0]
    assert first['target']=='Stornoway' and first['oxides']['SiO2']=={'wt_percent':58.,'rmsep_wt_percent':5.2}


def test_chemistry_is_region_scoped_and_scripts_available():
    client=server.application().test_client()
    assert len(client.get('/api/chemistry?region=glen-torridon').json['observations'])==878
    assert client.get('/api/chemistry?region=jezero').json['observations']==[]
    for module in ('mission','chemistry'):assert client.get('/field/'+module+'.js').status_code==200


def test_equipment_and_dust_scale_time_without_changing_default_routes():
    client=server.application().test_client()
    body=dict(start=[30,30],end=[30,40],max_slope_deg=40)
    baseline=client.post('/api/route?region=glen-torridon',json=body).json
    result=client.post('/api/route?region=glen-torridon',json={**body,
        'mission':dict(science_goal='Compare vein oxides',equipment='camera',equipment_mass_kg=12,mobility_factor=.5),
        'environment':dict(weather='dust')})
    assert result.status_code==200;data=result.json
    assert data['mission']['science_goal']=='Compare vein oxides' and data['mission']['crew_size']==2
    assert data['scenario']['values']['v_nom_flat_kmh']==pytest.approx(.8)
    for profile in ('fast','safe'):
        assert data[profile]['points']==baseline[profile]['points']
        assert data[profile]['time_s']==pytest.approx(baseline[profile]['time_s']/ .4,abs=.3)
    # Descriptive kg alone must not invent a physiological time penalty.
    mass=client.post('/api/route?region=glen-torridon',json={**body,'mission':{'equipment_mass_kg':50}}).json
    assert mass['fast']['time_s']==baseline['fast']['time_s']


@pytest.mark.parametrize('mission',[[],{'science_goal':'a'*601},{'mobility_factor':0},{'mobility_factor':True},{'equipment_mass_kg':float('nan')},{'equipment':42}])
def test_mission_validation(mission):
    with pytest.raises(ValueError):mission_context(mission)


def test_manual_obstacle_exclusion_blocks_path_without_changing_terrain():
    arrays=tuple(np.zeros((7,7)) for _ in range(3))
    masked,environment=apply_environment(arrays,{'blocked_zones':[{'center':[3,3],'radius_m':30,'kind':'manual_obstacle'}]},4,4)
    assert np.isnan(masked[0]).all() and np.isfinite(arrays[0]).all()
    assert environment['blocked_zones'][0]['kind']=='manual_obstacle'
