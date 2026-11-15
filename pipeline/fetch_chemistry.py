"""Reproduce spatially matched MSL/ChemCam oxide observations from NASA PDS."""
import csv
from datetime import datetime, timezone
import hashlib
import io
import json
import math
from pathlib import Path
from urllib.request import urlopen

BASE='https://pds-geosciences.wustl.edu/msl/urn-nasa-pds-msl_gt_diagenesis_supplement/data/'
OXIDES=('SiO2','TiO2','Al2O3','FeOT','MgO','CaO','Na2O','K2O','MnO')

def records(raw, category, targets):
    stream=io.StringIO(raw.decode('utf-8-sig'));stream.readline()
    reader=csv.DictReader(stream)
    # Match both target name and sequence, never nearest name/location alone.
    lookup={(t['name'],t['sequence']):t for t in targets}
    result=[];unmatched=0
    def number(value):
        try:n=float(value)
        except (ValueError,TypeError):return None
        return n if math.isfinite(n) and n>=0 else None
    for row in reader:
        target=lookup.get((row['Target'],row['SeqID']))
        if target is None:unmatched+=1;continue
        values={oxide:{'wt_percent':number(row.get(oxide)),
                       'rmsep_wt_percent':number(row.get(oxide+' RMSEP'))} for oxide in OXIDES}
        result.append(dict(id=f"{category}:{row['File']}:{row['Observation Point']}",target_id=target['id'],
            target=target['name'],sequence=target['sequence'],sol=int(row['Sol']),
            lat=target['lat'],lon=target['lon'],category=category,observation_point=row['Observation Point'],
            source_file=row['File'],oxides=values))
    return result,unmatched


def prepare(output='assets/glen-torridon'):
    output=Path(output);targets=json.loads((output/'targets.json').read_text())
    observations=[];sources=[]
    for name in ('veins','nodules'):
        for extension in ('csv','xml'):
            url=BASE+name+'.'+extension
            raw=urlopen(url,timeout=60).read()
            (output/('chemistry_'+name+'.'+extension)).write_bytes(raw)
            entry=dict(url=url,sha256=hashlib.sha256(raw).hexdigest(),bytes=len(raw))
            if extension=='csv':
                parsed,unmatched=records(raw,name,targets);observations+=parsed
                entry.update(matched_observations=len(parsed),unmatched_observations=unmatched)
            sources.append(entry)
    document=dict(schema_version=1,instrument='MSL Curiosity / ChemCam LIBS',
        data_kind='chemical',units='wt%',uncertainty='RMSEP of prediction, wt%; not a confidence interval',
        spatial_assignment='Target coordinates, not separately resolved laser spot coordinates',
        observation_period='2019-01-24 / 2021-01-16 (PDS vein table coverage; see individual sols and labels)',
        retrieved_at=datetime.now(timezone.utc).isoformat(),sources=sources,observations=observations)
    (output/'chemistry.json').write_text(json.dumps(document,ensure_ascii=False,separators=(',',':')))
    print(f'{len(observations)} ChemCam observations, {len(set(o["target_id"] for o in observations))} matched targets')

if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',default='assets/glen-torridon');prepare(p.parse_args().out)
