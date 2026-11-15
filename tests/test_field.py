import json
from pathlib import Path
import numpy as np
import pytest
import server
from pipeline.route import plan_routes
from pipeline.restrictions import apply_environment


def plan(dem, slope, **kwargs):
    return plan_routes(dem, slope, np.zeros_like(dem), 4, [2, 1], [2, 5], 30000, 60, [], .15, **kwargs)


def test_adjustable_slope_and_independent_home():
    dem=np.tile(np.arange(7)*.6,(7,1)); slope=np.full_like(dem,9.)
    with pytest.raises(ValueError): plan(dem,slope,max_slope_deg=5)
    for route in plan(dem,slope,max_slope_deg=40,return_to=[2,0]).values():
        assert route['return']['points'][-1]==[2,0]
        assert route['ascent_m']>0 and route['descent_m']>0
    for route in plan(dem,slope,return_to=[2,5]).values():
        assert route['end_is_home']
        assert route['return']['distance_m']==0


def test_restrictions_preserve_data_and_can_prevent_passage():
    arrays=tuple(np.zeros((7,7)) for _ in range(3))
    masked,meta=apply_environment(arrays,{'weather':'storm','blocked_zones':[{'center':[3,3],'radius_m':30}]},4,4)
    assert meta['speed_factor']==.6
    assert np.isfinite(arrays[0]).all() and np.isnan(masked[0]).all()
    with pytest.raises(ValueError):plan(masked[0],masked[1])


def test_catalog_scope_and_field_scripts():
    client=server.application().test_client()
    records=client.get('/api/science-targets?region=glen-torridon').json['targets']
    assert len(records)==718
    assert all({'lat','lon','sol','formation','veins','nodules'}<=r.keys() for r in records)
    assert client.get('/api/science-targets?region=jezero').json['targets']==[]
    assert client.get('/api/terrain?region=glen-torridon').status_code==200
    assert client.get('/reference-image?region=glen-torridon').status_code==200
    for name in ['points','operations','actors','texture','navigator']:
        assert client.get('/field/'+name+'.js').status_code==200
    assert client.post('/api/route',json={'start':[0,0],'end':[1,1],'science_waypoints':[[2,2]]*19}).status_code==400
