"""Bound PCG generation volumes to all preserved terrain in each XY tile."""
import hashlib,json
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[2];SRC=ROOT/'Content/World/Starter/Source'
raw=SRC/'HM_StarterRegion.r16';sha=hashlib.sha256(raw.read_bytes()).hexdigest()
assert sha=='bc3a7957a44629beb807d4a453443e9d02e48981be0cfb93d5054432c077f8c8'
h=(np.fromfile(raw,dtype='<u2').reshape(4033,4033).astype('f4')-32768)*.0625
rollout=json.loads((ROOT/'Saved/Validation/StarterWorld/Phase2/forest-rollout.json').read_text())
native=json.loads((ROOT/'Saved/Validation/StarterWorld/Structure/forest-bounds.json').read_text())
existing={t['label'] for t in rollout['tiles']}
for a in native:
    if a['label'].startswith('PCG_Forest_') and a['label'] not in existing:
        i,j=map(int,a['label'].removeprefix('PCG_Forest_').split('_'))
        rollout['tiles'].append(dict(label=a['label'],bounds_m=[i*512,j*512,min((i+1)*512,8064),min((j+1)*512,8064)]))
tiles={}
for t in rollout['tiles']:
    x0,y0,x1,y1=t['bounds_m'];q=h[int(y0//2):min(4033,int(y1//2)+1),int(x0//2):min(4033,int(x1//2)+1)]
    low=float(q.min())-5;high=float(q.max())+85
    tiles[t['label']]=dict(xy_bounds_m=t['bounds_m'],bottom_m=low,top_m=high,center_z_cm=(low+high)*50,half_height_m=(high-low)/2)
p=dict(schema=1,height_sha256=sha,height_modified=False,policy='Full XY tile terrain extrema plus -5/+85m safety; baked world transforms preserved; manual editor regeneration',tiles=tiles)
(SRC/'StarterWorld.forest-streaming.json').write_text(json.dumps(p,indent=2)+'\n')
print('Planned',len(tiles),'bounded forest volumes')
