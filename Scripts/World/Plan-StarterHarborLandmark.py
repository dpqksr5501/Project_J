"""Fit a native watchtower kit on dry upper-bank terrain, preserving all plots."""
import importlib.util,json,math
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source';FILE=SOURCE/'StarterWorld.settlements.json'
spec=importlib.util.spec_from_file_location('terrain',ROOT/'Scripts/World/Starter-TerrainProfile.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);T=m.TerrainProfile(SOURCE/'StarterRegion.manifest.json')
data=json.loads(FILE.read_text(encoding='utf-8'));p=next(p for p in data['settlements'] if p['target_id']=='S05');assert p['stage']=='PLANNED' and 'watchtower' not in p
cx,cy=next(v['center_m'][:2] for v in json.loads((SOURCE/'StarterWorld.plan.json').read_text(encoding='utf-8'))['places'] if v['id']==p['stable_id']);size=11.1;candidates=[]
for dx in range(70,151,3):
    for dy in range(-100,11,3):
        if math.hypot(dx,dy)>170:continue
        if any(math.dist([dx,dy],h['offset_m'])<math.hypot(h['width'],h['depth'])/2+size/2+4 for h in p['houses']):continue
        samples=[T.height(cx+dx+u,cy+dy+v) for u in [-size/2,0,size/2] for v in [-size/2,0,size/2]];relief=max(samples)-min(samples)
        if relief>2.5:continue
        candidates.append((relief*10+math.dist([dx,dy],[110,-70]),dx,dy,relief))
assert candidates
_,dx,dy,relief=min(candidates);p['watchtower']=dict(offset_m=[dx,dy],mesh='/Game/Map_Resource/MedCastle/Mesh/Houses_Towers/SM_MainTower2Doors',scale=.7,foundation_width_m=size+.2,footprint_relief_m=relief,role='Harbor lookout landmark; interior/lighting interaction pending')
p['work_yards']=[dict(id='CargoCourt',offset_m=[-25,-30],width_m=18,depth_m=16),dict(id='LookoutClearing',offset_m=[dx,dy],width_m=size+1,depth_m=size+1)]
FILE.write_text(json.dumps(data,indent=2),encoding='utf-8');print('Watchtower plot',p['watchtower'])
