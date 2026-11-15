"""Global MOLA sampling and small, meter-based regional windows on Mars."""
from functools import lru_cache
import json
import math
from pathlib import Path

import numpy as np

ASSETS=Path(__file__).resolve().parent.parent/'assets'/'mars'
RADIUS=3396000.0
GRID=81
CELL=5000.0


class RegionError(ValueError):
    pass


def parse_region(value):
    try:
        provider,latitude,longitude=value.split(':')
        lat,lon=float(latitude),float(longitude)
        if provider!='mola' or not math.isfinite(lat) or not math.isfinite(lon) or not -90<=lat<=90:
            raise ValueError()
        lon=(lon+180)%360-180
        return round(lat,4),round(lon,4)
    except (ValueError,TypeError,AttributeError):
        raise RegionError('Región inválida: usa mola:latitud:longitud, latitud −90…90 y longitud este.') from None


@lru_cache(maxsize=1)
def global_dem():
    path=ASSETS/'mola16.npy'
    if not path.is_file():raise RegionError('Falta el modelo MOLA global en el servidor.')
    return np.load(path,mmap_mode='r',allow_pickle=False)


def geographic_grid(latitude,longitude,size=GRID,cell=CELL):
    """Inverse spherical azimuthal equidistant projection, including both poles."""
    axis=(np.arange(size)-(size-1)/2)*cell
    x,y=np.meshgrid(axis,-axis)
    rho=np.hypot(x,y);angle=rho/RADIUS;safe=np.where(rho==0,1,rho)
    lat0,lon0=np.radians([latitude,longitude])
    lat=np.arcsin(np.clip(np.cos(angle)*np.sin(lat0)+y*np.sin(angle)*np.cos(lat0)/safe,-1,1))
    lon=lon0+np.arctan2(x*np.sin(angle),rho*np.cos(lat0)*np.cos(angle)-y*np.sin(lat0)*np.sin(angle))
    lat=np.where(rho==0,lat0,lat);lon=np.where(rho==0,lon0,lon)
    return np.degrees(lat),(np.degrees(lon)+180)%360-180


def sample_global(latitudes,longitudes,dem=None):
    dem=global_dem() if dem is None else dem
    rows,cols=dem.shape
    r=np.clip((90-np.asarray(latitudes))*rows/180-.5,0,rows-1)
    c=(np.mod(longitudes,360)*cols/360-.5)%cols
    r0=np.floor(r).astype(int);r1=np.minimum(r0+1,rows-1)
    c0=np.floor(c).astype(int);c1=(c0+1)%cols
    t,u=r-r0,c-c0
    z=(dem[r0,c0]*(1-u)+dem[r0,c1]*u)*(1-t)+(dem[r1,c0]*(1-u)+dem[r1,c1]*u)*t
    # A single pole cannot have multiple heights depending on longitude.
    z=np.where(np.asarray(latitudes)>=90-1e-8,float(dem[0].mean()),z)
    z=np.where(np.asarray(latitudes)<=-90+1e-8,float(dem[-1].mean()),z)
    return z.astype(np.float32)


@lru_cache(maxsize=8)
def regional_arrays(latitude,longitude):
    from pipeline.terrain import slope_deg
    from pipeline.roughness import rough_from_dem
    lat,lon=geographic_grid(latitude,longitude)
    z=sample_global(lat,lon)
    slope=slope_deg(z,CELL,CELL)
    rough=rough_from_dem(z)
    for array in (z,slope,rough):array.setflags(write=False)
    return z,slope,rough


def materialize_region(directory,latitude,longitude):
    from pipeline.manifest import write_manifest
    from pipeline.scenario import scenario
    from pipeline.tiles import save_png,to_u8
    z,slope,rough=regional_arrays(latitude,longitude)
    values=scenario()
    for name,array in [('dem',z),('slope',slope),('rough',rough)]:np.save(directory/(name+'.npy'),array,allow_pickle=False)
    save_png(to_u8(slope),directory/'preview.png',size=(GRID,GRID))
    manifest=write_manifest(directory/'routes.json',site='mola-global',waypoints=[],layers=[],routes={},
      budget_s=values['suit_budget_s'],science_s=values['science_stop_s'],cell_m=CELL,cell_x_m=CELL,
      grid={'rows':GRID,'cols':GRID},dem_validity='nodata-preserved-v1',
      spatial={'crs':'MARS:SPHERICAL_AEQD','center_latitude_deg':latitude,'center_longitude_deg':longitude,
        'radius_m':RADIUS,'bounds':[-GRID*CELL/2,-GRID*CELL/2,GRID*CELL/2,GRID*CELL/2],
        'latitude_type':'planetocentric','longitude_direction':'east','projected_approximation':True})
    provenance=json.loads((ASSETS/'metadata.json').read_text())
    provenance.update(source_resolution_m=3705,derived_resolution_m=CELL,
      interpolation='Bilinear resampling from 16 pixels/degree MOLA bins into a local spherical azimuthal equidistant grid. The underlying PDS product also contains interpolated bins.')
    manifest.update(region=f'mola:{latitude:.4f}:{longitude:.4f}',planning_scope='regional_overview',
      provenance=provenance,note='MOLA regional overview only. Resolution is unsuitable for EVA walking, rocks, sand or small hazards. Budget and guidance remain scenario simulations, not operational recommendations.')
    (directory/'routes.json').write_text(json.dumps(manifest),encoding='utf-8')


@lru_cache(maxsize=1)
def atlas():
    dem=global_dem()
    rows=np.linspace(0,dem.shape[0]-1,121).astype(int)
    cols=np.linspace(0,dem.shape[1]-1,241).astype(int)
    return {'latitudes':(90-(rows+.5)*180/dem.shape[0]).tolist(),
      'longitudes':((cols+.5)*360/dem.shape[1]).tolist(),
      'elevations':dem[np.ix_(rows,cols)].tolist(),
      'metadata':json.loads((ASSETS/'metadata.json').read_text()),
      'bookmarks':[{'name':'Jezero · HiRISE 4 m','region':'jezero','lat':18.4881,'lon':77.388},
        {'name':'Gale / Curiosity · MOLA','lat':-5.4,'lon':137.8},
        {'name':'Olympus Mons · MOLA','lat':18.65,'lon':-133.8},
        {'name':'Valles Marineris · MOLA','lat':-13.9,'lon':-59.0},
        {'name':'Hellas Planitia · MOLA','lat':-42.4,'lon':70.5},
        {'name':'Polo norte · MOLA','lat':90,'lon':0},{'name':'Polo sur · MOLA','lat':-90,'lon':0}]}
