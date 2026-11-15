"""Download the global MOLA 16 pixels/degree topography product from NASA PDS."""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
from urllib.request import urlopen

import numpy as np

BASE='https://pds-geosciences.wustl.edu/mgs/mgs-m-mola-5-megdr-l3-v1/mgsl_300x/meg016/'
PRODUCT='megt90n000eb'
SHAPE=(2880,5760)


def prepare(output):
    output=Path(output)
    if output.exists():raise ValueError('Usa una carpeta nueva; no se reemplazarán datos.')
    label=urlopen(BASE+PRODUCT+'.lbl',timeout=45).read(16384)
    text=label.decode('ascii')
    for expected in ['LINES                        = 2880','LINE_SAMPLES                 = 5760','SAMPLE_TYPE                  = MSB_INTEGER','SAMPLE_BITS                  = 16']:
        if expected not in text:raise ValueError('El producto PDS cambió de formato.')
    expected_size=SHAPE[0]*SHAPE[1]*2
    with urlopen(BASE+PRODUCT+'.img',timeout=60) as response:
        raw=response.read(expected_size+1)
    if len(raw)!=expected_size:raise ValueError('El producto global está incompleto.')
    dem=np.frombuffer(raw,dtype='>i2').reshape(SHAPE).astype('<i2')
    output.mkdir(parents=True)
    np.save(output/'mola16.npy',dem,allow_pickle=False)
    (output/'mola16.lbl').write_bytes(label)
    metadata={'dataset_id':'MGS-M-MOLA-5-MEGDR-L3-V1.0','product_id':'MEGT90N000EB.IMG',
      'source_url':BASE+PRODUCT+'.img','label_url':BASE+PRODUCT+'.lbl',
      'product_url':'https://pds-geosciences.wustl.edu/missions/mgs/megdr.html',
      'retrieved_at':datetime.now(timezone.utc).isoformat(),'source_sha256':hashlib.sha256(raw).hexdigest(),
      'derived_sha256':hashlib.sha256((output/'mola16.npy').read_bytes()).hexdigest(),
      'shape':list(SHAPE),'pixels_per_degree':16,'native_latitude_spacing_deg':0.0625,
      'equatorial_resolution_m':3705,'radius_m':3396000,'longitude_direction':'east',
      'latitude_type':'planetocentric','vertical_reference':'MOLA areoid GMM3 / IAU2000',
      'credit':'NASA/GSFC/MGS MOLA Science Team; NASA PDS Geosciences Node',
      'limitations':'Regional context only. Bins without observations were interpolated by the producer; gaps between profiles can reach 12 km. Not a walking hazard map.'}
    (output/'metadata.json').write_text(json.dumps(metadata,indent=2))
    print('MOLA global guardado:',output,'rango:',int(dem.min()),int(dem.max()),flush=True)


if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--out',default='assets/mars')
    prepare(parser.parse_args().out)
