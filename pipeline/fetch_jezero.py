"""Prepare the real Jezero crop from the official USGS S3 distribution.

Reads GeoTIFF metadata and the necessary scanline ranges, never the whole 1.8 GB file.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
from datetime import datetime, timezone
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from urllib.request import Request, urlopen

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ('https://asc-pds-services.s3.us-west-2.amazonaws.com/mosaic/mars2020_trn/HiRISE/'
          'JEZ_hirise_soc_006_DTM_MOLAtopography_DeltaGeoid_1m_Eqc_latTs0_lon0_blend40.tif')
PRODUCT = 'https://astrogeology.usgs.gov/search/map/mars_2020_terrain_relative_navigation_hirise_dtm_mosaic'


def read_range(start, end):
    request = Request(SOURCE + f'?window_range={start}-{end}', headers={'Range': f'bytes={start}-{end}'})
    with urlopen(request, timeout=60) as response:
        content_range = response.headers.get('Content-Range', '')
        if response.status != 206 or not content_range.startswith(f'bytes {start}-{end}/'):
            raise ValueError('El distribuidor no respetó la descarga parcial; no se descargará el TIFF completo.')
        data = response.read(end - start + 2)
    if len(data) != end - start + 1:
        raise ValueError('Respuesta parcial incompleta.')
    return data


def strip_layout(header):
    """Only accept the documented uncompressed, single-band classic TIFF layout."""
    endian = {b'II': '<', b'MM': '>'}.get(header[:2])
    if endian is None or struct.unpack_from(endian+'H', header, 2)[0] != 42:
        raise ValueError('Formato GeoTIFF remoto inesperado.')
    ifd = struct.unpack_from(endian+'I', header, 4)[0]
    count = struct.unpack_from(endian+'H', header, ifd)[0]
    tags = {}
    for index in range(count):
        tag, kind, size, offset = struct.unpack_from(endian+'HHII', header, ifd+2+12*index)
        tags[tag] = kind, size, offset
    def integers(tag):
        kind, size, offset = tags[tag]
        if kind != 4 or offset+size*4 > len(header):
            raise ValueError('La tabla de filas excede los metadatos esperados.')
        return struct.unpack_from(endian+str(size)+'I', header, offset)
    return endian, integers(273), integers(279)


def fetch_crop(path, reference):
    import rasterio
    from rasterio.io import MemoryFile
    from rasterio.windows import Window

    header = read_range(0, 262143)
    endian, offsets, counts = strip_layout(header)
    window = reference['dem_source_window']
    r0, c0, height, width = (window[k] for k in ('row', 'col', 'height', 'width'))
    with MemoryFile(header) as memory:
        with memory.open() as source:
            if (source.count != 1 or source.dtypes != ('float32',) or source.block_shapes != [(1, source.width)]
                    or any(count != source.width*4 for count in counts)
                    or source.compression is not None):
                raise ValueError('Cambió la estructura del DEM remoto. Revisa el importador.')
            bounds = source.window_bounds(Window(c0, r0, width, height))
            if source.crs != rasterio.crs.CRS.from_string(reference['crs']) or not np.allclose(bounds, reference['bounds_m'], atol=.01, rtol=0):
                raise ValueError('La ventana real no coincide con la ortofoto de referencia.')
            profile = source.profile.copy()
            profile.update(width=width, height=height, transform=source.window_transform(Window(c0,r0,width,height)), compress='deflate')
            source_width = source.width
    result = np.empty((height, width), dtype=np.float32)
    def fetch_rows(first):
        last = min(first+64, r0+height)
        if any(offsets[row+1] != offsets[row]+counts[row] for row in range(first,last-1)):
            raise ValueError('Las filas remotas dejaron de ser contiguas.')
        raw = read_range(offsets[first], offsets[last-1]+counts[last-1]-1)
        rows = np.frombuffer(raw, dtype=endian+'f4').reshape(last-first, source_width)
        return first, rows[:,c0:c0+width].copy(), len(raw)
    downloaded = len(header)
    with ThreadPoolExecutor(max_workers=4) as executor:
        for first, rows, size in executor.map(fetch_rows, range(r0,r0+height,64)):
            result[first-r0:first-r0+len(rows)] = rows
            downloaded += size
    with rasterio.open(path, 'w', **profile) as output:
        output.write(result, 1)
    return {'retrieved_at': datetime.now(timezone.utc).isoformat(), 'source_url': SOURCE, 'product_url': PRODUCT, 'doi': '10.5066/P9REJ9JN',
            'source_window': window, 'source_resolution_m': 1,
            'transferred_bytes': downloaded,
            'crop_sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def prepare(output):
    output = Path(output)
    if output.exists():
        raise ValueError(f'{output} ya existe. Usa una carpeta nueva para conservar tus datos.')
    reference = json.loads((ROOT/'assets/jezero_ortho_4m.json').read_text())
    with tempfile.TemporaryDirectory(prefix='jezero-') as temporary:
        directory=Path(temporary)
        source=directory/'jez_dtm_1m.tif'
        print('Descargando la ventana HiRISE de Jezero por rangos…', flush=True)
        provenance=fetch_crop(source, reference)
        build=directory/'build'
        subprocess.run([sys.executable,'-m','pipeline.build','--dem',str(source),'--rows','500','--cols','500','--cell','4','--out',str(build)],cwd=ROOT,check=True)
        manifest=json.loads((build/'routes.json').read_text())
        manifest['spatial']['source_window']=reference['dem_source_window']
        manifest['provenance']=provenance
        manifest['provenance']['credit']=reference['credit']
        manifest['provenance']['derived_resolution_m']=4
        manifest['provenance']['image_alignment']='Matching declared CRS, bounds and source window; absolute horizontal accuracy not independently verified.'
        (build/'routes.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2))
        import shutil
        output.mkdir(parents=True)
        for name in ('routes.json','dem.npy','slope.npy','rough.npy','preview.png'):
            shutil.copy2(build/name,output/name)
        provenance['derived_sha256']={name:hashlib.sha256((output/name).read_bytes()).hexdigest() for name in ('dem.npy','slope.npy','rough.npy','preview.png','routes.json')}
        (output/'provenance.json').write_text(json.dumps(provenance,indent=2))
    print(f'Terreno real preparado en {output}',flush=True)


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',default='data/jezero')
    args=parser.parse_args()
    prepare(args.out)
