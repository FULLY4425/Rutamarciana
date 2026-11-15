import hashlib
import json
from pathlib import Path
import tempfile

import numpy as np
import pytest

from pipeline.planet import (ASSETS,CELL,GRID,RADIUS,RegionError,geographic_grid,
                            parse_region,sample_global)
from server import application


def test_global_dataset_integrity_and_declared_dimensions():
    metadata=json.loads((ASSETS/'metadata.json').read_text())
    assert hashlib.sha256((ASSETS/'mola16.npy').read_bytes()).hexdigest()==metadata['derived_sha256']
    dem=np.load(ASSETS/'mola16.npy',mmap_mode='r',allow_pickle=False)
    assert dem.shape==(2880,5760)
    assert int(dem.min())==-8177 and int(dem.max())==21171
    assert metadata['latitude_type']=='planetocentric'


@pytest.mark.parametrize('value',['mola:nan:0','mola:0:inf','mola:90.1:0','mola:-90.1:0','../../other','mola:0:0:1','other:0:0'])
def test_invalid_regions_are_rejected(value):
    with pytest.raises(RegionError):parse_region(value)


def test_longitude_is_normalized_without_latitude_aliasing():
    assert parse_region('mola:0:180')==(0,-180)
    assert parse_region('mola:0:540')==(0,-180)
    assert parse_region('mola:90:360')==(90,0)


@pytest.mark.parametrize('lat,lon',[(0,179.99),(90,180),(-90,-180),(18.65,-133.8)])
def test_local_geography_includes_poles_and_antimeridian(lat,lon):
    latitudes,longitudes=geographic_grid(lat,lon)
    center=(GRID-1)//2
    assert latitudes[center,center]==pytest.approx(lat)
    assert longitudes[center,center]==pytest.approx((lon+180)%360-180)
    assert np.isfinite(latitudes).all() and np.isfinite(longitudes).all()
    assert np.all(np.abs(latitudes)<=90) and np.all(np.abs(longitudes)<=180)
    # Radial distance in an azimuthal equidistant grid equals great-circle distance.
    p1,p2=np.radians(lat),np.radians(latitudes[center,center+1])
    dl=np.radians(longitudes[center,center+1]-lon)
    distance=2*RADIUS*np.arcsin(np.sqrt(np.sin((p2-p1)/2)**2+np.cos(p1)*np.cos(p2)*np.sin(dl/2)**2))
    assert distance==pytest.approx(CELL,rel=1e-5)
    z=sample_global(latitudes,longitudes)
    assert z.shape==(GRID,GRID) and np.isfinite(z).all()


def test_global_sampling_wraps_and_poles_have_single_heights():
    assert sample_global(np.array([0,0,0]),np.array([-180,180,540])).tolist()==[sample_global(np.array([0]),np.array([180]))[0]]*3
    north=sample_global(np.full(4,90),np.array([0,90,180,270]))
    np.testing.assert_allclose(north,np.full(4,north[0]))


def test_requests_keep_regions_isolated_and_clean_up_temporary_files():
    client=application().test_client()
    before=set(Path(tempfile.gettempdir()).glob('mars-region-*'))
    region='mola:0:179.99'
    meta=client.get('/api/routes?region='+region)
    assert meta.status_code==200 and meta.json['scenario']['site']=='Mars/global'
    terrain=client.get('/api/terrain?region='+region).json
    assert terrain['site']=='mola-global' and terrain['cell_m']==5000
    assert terrain['planning_scope']=='regional_overview'
    assert client.get('/api/terrain-image?layer=elevation&region='+region).status_code==200
    assert client.get('/api/manifest?full=1&region='+region).status_code==200
    assert client.get('/api/routes').json['site']=='jez_dtm_1m'
    assert client.get('/api/terrain?region=mola:91:0').status_code==400
    assert set(Path(tempfile.gettempdir()).glob('mars-region-*'))==before


def test_existing_routing_and_science_contract_works_on_mola_regions():
    client=application().test_client()
    response=client.post('/api/route?region=mola:90:0',json={
        'schema_version':3,'start':[40,40],'end':[42,42],'science_waypoints':[[41,40]]})
    assert response.status_code==200,response.json
    data=response.json
    assert data['site']=='mola-global' and data['region']=='mola:90.0000:0.0000'
    for name in ['safe','fast']:
        assert data[name]['return']['points'][-1]==[40,40]
        assert data[name]['outbound']['legs'][0]['points'][-1]==[41,40]
    assert data['spatial']['crs']=='MARS:SPHERICAL_AEQD'
    assert data['planning_scope']=='regional_overview'
