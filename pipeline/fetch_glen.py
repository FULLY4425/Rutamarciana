"""Prepare Glen Torridon and its 718 targets directly from NASA/PDS and HiRISE."""
import csv
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime,timezone
import hashlib
import io
import json
import math
from pathlib import Path
import struct
import tempfile
from urllib.request import Request,urlopen

import numpy as np

BASE='https://pds-geosciences.wustl.edu/msl/urn-nasa-pds-msl_gt_diagenesis_supplement/'
HIRISE='https://www.uahirise.org/PDS/DTM/PSP/ORB_009100_009199/PSP_009149_1750_PSP_009294_1750/'
DTM=HIRISE+'DTEEC_009149_1750_009294_1750_U01.IMG'
ORTHO=HIRISE+'PSP_009149_1750_RED_C_01_ORTHO.JP2'

def get(url,byte_range=None):
    request=Request(url+(f'?window={byte_range[0]}' if byte_range else ''),headers={'Range':f'bytes={byte_range[0]}-{byte_range[1]}'} if byte_range else {})
    with urlopen(request,timeout=90) as response:
        if byte_range and response.status!=206:raise ValueError('HiRISE no respetó la descarga parcial.')
        return response.read(byte_range[1]-byte_range[0]+2 if byte_range else 100_000_000)

def prepare(output):
    from scipy.ndimage import maximum_filter
    from pipeline.manifest import write_manifest
    from pipeline.scenario import scenario
    from pipeline.terrain import slope_deg
    from pipeline.roughness import rough_from_dem
    from pipeline.tiles import save_png,to_u8
    import rasterio
    from rasterio.windows import Window
    from PIL import Image
    output=Path(output)
    if output.exists():raise ValueError('Usa una carpeta nueva para conservar los datos.')
    start,end=2800*26996,3653*26996-1
    with ThreadPoolExecutor(max_workers=4) as executor:
        futures=[executor.submit(get,BASE+'data/target_classification.csv'),executor.submit(get,DTM,(start,end)),executor.submit(get,ORTHO),executor.submit(get,HIRISE+'PSP_009149_1750_RED_C_01_ORTHO.LBL')]
        raw,window,jp2,label=[f.result() for f in futures]
    if len(window)!=end-start+1:raise ValueError('Ventana HiRISE incompleta.')
    rows=list(csv.DictReader(io.StringIO('\n'.join(raw.decode('utf-8-sig').splitlines()[1:]))))
    targets=[dict(id=f'glen-{i+1}',name=r['Target'],sol=int(r['Sol']),lat=float(r['Latitude (dd)']),lon=float(r['Longitude (dd)']),elevation=float(r['Elevation (m)']),formation=r['Formation'],member=r['Stratigraphic Member'],veins=r['RMI has veins']=='1',nodules=r['RMI has nodules']=='1',sequence=r['SeqID']) for i,r in enumerate(rows)]
    if len(targets)!=718:raise ValueError('El catálogo ya no tiene los 718 registros esperados; revisa la versión.')
    z=np.array([[struct.unpack_from('<f',window,(line-2800)*26996+(col-1)*4)[0] for col in range(1400,2853,12)] for line in range(2800,3653,12)],dtype=np.float32)
    z[(z<-1e20)|~np.isfinite(z)]=np.nan
    step=12/58586.494013;cell=12*1.0117467945795
    west=137.41+(1399-3319.5)/58586.494013;north=(-273961.5-2799)/58586.494013
    cellx=cell*math.cos(math.radians(north-z.shape[0]*step/2))
    filled=np.where(np.isfinite(z),z,np.nanmedian(z));affected=maximum_filter(~np.isfinite(z),size=3)
    slope=slope_deg(filled,cell,cellx);slope[affected]=np.nan;rough=rough_from_dem(filled,valid=~affected)
    output.mkdir(parents=True)
    for name,array in [('dem',z),('slope',slope),('rough',rough)]:np.save(output/(name+'.npy'),array,allow_pickle=False)
    save_png(to_u8(slope),output/'preview.png')
    with tempfile.TemporaryDirectory() as temporary:
        source=Path(temporary)/'ortho.jp2';source.write_bytes(jp2)
        with rasterio.open(source) as image:
            pixels=image.read(1,window=Window(1399,2799,1464,864))
            if pixels.shape!=(864,1464):raise ValueError('La ortofoto cambió de geometría.')
        Image.fromarray(pixels).save(output/'ortho.png')
    radius=3396190;scale=math.pi/180*radius
    bounds=[west*scale,(north-z.shape[0]*step)*scale,(west+z.shape[1]*step)*scale,north*scale]
    spatial={'crs':'MARS:GLEN_EQC','bounds':bounds,'source_window':{'row':2799,'col':1399,'height':864,'width':1464},'west_deg':west,'north_deg':north,'degree_step':step,'latitude_type':'planetocentric','longitude_direction':'east'}
    imagery={'crs':spatial['crs'],'bounds_m':bounds,'dem_source_window':spatial['source_window'],'credit':'NASA/JPL/University of Arizona','source':ORTHO,'native_resolution_m':1.0117467945795,'note':'RED-band grayscale observation. Matching PDS projection/window; absolute horizontal accuracy not independently verified.'}
    provenance={'source_url':DTM,'product_url':'https://www.uahirise.org/dtm/dtm.php?ID=PSP_009294_1750','source_resolution_m':1.0117467945795,'derived_resolution_m':cell,'credit':'NASA/JPL/University of Arizona; NASA PDS Curiosity/ChemCam, Gasda et al.','target_source':BASE,'target_count':718,'retrieved_at':datetime.now(timezone.utc).isoformat(),'csv_sha256':hashlib.sha256(raw).hexdigest(),'dtm_window_sha256':hashlib.sha256(window).hexdigest(),'ortho_source_sha256':hashlib.sha256(jp2).hexdigest(),'ortho_crop_sha256':hashlib.sha256((output/'ortho.png').read_bytes()).hexdigest(),'sampling':'Native DTM sampled every 12 pixels; NoData preserved. Photographic crop from the matching PDS ortho product.'}
    values=scenario()
    manifest=write_manifest(output/'routes.json',site='glen-torridon',waypoints=[],layers=[],routes={},budget_s=values['suit_budget_s'],science_s=values['science_stop_s'],cell_m=cell,cell_x_m=cellx,grid={'rows':z.shape[0],'cols':z.shape[1]},spatial=spatial,dem_validity='nodata-preserved-v1')
    manifest.update(region='glen-torridon',provenance=provenance,planning_scope='local_terrain')
    (output/'routes.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2))
    (output/'targets.json').write_text(json.dumps(targets,ensure_ascii=False))
    (output/'imagery.json').write_text(json.dumps(imagery,indent=2))
    (output/'target_classification.csv').write_bytes(raw);(output/'ortho.lbl').write_bytes(label)
    (output/'provenance.json').write_text(json.dumps(provenance,indent=2))
    print(f'Glen Torridon: {len(targets)} objetivos, DEM {z.shape}, ortofoto {pixels.shape}.',flush=True)

if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',default='assets/glen-torridon');prepare(p.parse_args().out)
