"""Complement preserved lowland forests with terrain-aware mixed/montane habitat.

No height edits or PCG regeneration. Saved native additions are spatial chunks;
all authored placement exclusions, current canopy, water and expansion reserve
are respected. Deliberate glades and ridgeline exposure remain open.
"""
import argparse,collections,hashlib,importlib.util,json,math
from pathlib import Path
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[2];SRC=ROOT/'Content/World/Starter/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Structure';FILE=SRC/'StarterWorld.natural-diversity.json'
parser=argparse.ArgumentParser();parser.add_argument('--replace-plan',action='store_true');args=parser.parse_args()
if FILE.exists():assert args.replace_plan and json.loads(FILE.read_text())['status']=='PLANNED' and not (OUT/'natural-diversity-build.json').exists(),'Inspect the existing authored pass; never regenerate silently'
spec=importlib.util.spec_from_file_location('terrain',Path(__file__).with_name('Starter-TerrainProfile.py'));T=importlib.util.module_from_spec(spec);spec.loader.exec_module(T);terrain=T.TerrainProfile(SRC/'StarterRegion.manifest.json')
rng=np.random.default_rng(103180);plan=json.loads((SRC/'StarterWorld.plan.json').read_text());layout=json.loads((SRC/'StarterWorld.corridor.json').read_text())
spec=importlib.util.spec_from_file_location('biomes',Path(__file__).with_name('Generate-StarterBiomeRules.py'));B=importlib.util.module_from_spec(spec);spec.loader.exec_module(B)
trees=json.loads((ROOT/'Saved/Validation/StarterWorld/Quality/final-restored-trees.json').read_text());occupied=collections.defaultdict(list)
for t in trees:
    x,y=t[0]/100+4032,t[1]/100+4032;occupied[(int(x//12),int(y//12))].append((x,y))
def clear(x,y,radius=9):
    if 3590<x<4460 and 4130<y<5020:return False # proposed capital reserve
    if any(math.hypot(x-p['center_m'][0],y-p['center_m'][1])<p['radius_m']+(25 if p['id'].startswith('S') else 12) for p in plan['places']):return False
    return all(math.hypot(x-a,y-b)>=radius for dj in [-1,0,1] for di in [-1,0,1] for a,b in occupied[(int(x//12)+di,int(y//12)+dj)])
def noise(x,y):return .5+.22*math.sin(x/167+math.sin(y/281)*1.8)+.17*math.sin(y/107+x/331)+.1*math.sin((x-y)/59)
def survey(x,y):
    z=terrain.height(x,y);gx=(terrain.height(x+2,y)-terrain.height(x-2,y))/4;gy=(terrain.height(x,y+2)-terrain.height(x,y-2))/4
    return z,gx,gy,math.hypot(gx,gy)
def river_clear(x,y,radius):
    for river in plan['rivers']:
        for a,b in zip(river['points'],river['points'][1:]):
            dx,dy=b[0]-a[0],b[1]-a[1];t=max(0,min(1,((x-a[0])*dx+(y-a[1])*dy)/max(dx*dx+dy*dy,1e-8)))
            if math.hypot(x-a[0]-t*dx,y-a[1]-t*dy)<river['width_m']/2+radius:return False
    return True
sky='/Game/Map_Resource/SkyTown/Meshes/';new='/Game/ProceduralNaturePack/Meshes/rocks_boulders/'
montane=json.loads((SRC/'StarterWorld.montane-palette.json').read_text())
instances=[];candidates=[]
for y0 in np.arange(32,8032,23):
    for x0 in np.arange(32,8032,23):
        x=float(x0+rng.uniform(-9,9));y=float(y0+rng.uniform(-9,9));z,gx,gy,slope=survey(x,y)
        if not 180<z<1080 or slope>.78 or not clear(x,y):continue
        patch=noise(x,y);threshold=.39 if z<720 else .49+(z-720)/1400
        if patch<threshold or rng.random()>(.72 if z>350 else .32):continue
        if not river_clear(x,y,18):continue
        candidates.append((x,y,z,slope))
cx=np.array([q[0] for q in candidates]);cy=np.array([q[1] for q in candidates]);clearance,water=B.exclusion_fields(cx,cy,plan,layout)
candidates=[q for q,c in zip(candidates,clearance) if c>.95]
rng.shuffle(candidates)
for x,y,z,slope in candidates[:14500]:
    if not clear(x,y,10):continue
    if z<420 and rng.random()<.56:
        name=rng.choice(['SM_AutumnTree01','SM_AutumnTree02','SM_AutumnTree03','SM_AutumnTree04']);scale=float(rng.uniform(.65,1.05))
    elif rng.random()<.3:
        name='SM_SpruceTree01';scale=float(rng.uniform(.43,.72))
    else:name='SM_SpruceTree02';scale=float(rng.uniform(.85,1.5))
    path=sky+str(name)
    if path in montane['mapping']:
        item=montane['mapping'][path];path=item['mesh'];scale*=item['scale_factor']
    instances.append(dict(kind='Tree',mesh=path,position_m=[x,y,z-.12],yaw=float(rng.uniform(0,360)),scale=[scale,scale,scale],sink_fraction=0));occupied[(int(x//12),int(y//12))].append((x,y))
# Sparse boulders anchor wooded foothills. Generic repeated cliff stamps were
# rejected in actual fixed-camera review; do not recreate that failed path.
rock_candidates=[]
for y0 in np.arange(70,7994,82):
    for x0 in np.arange(70,7994,82):
        x=float(x0+rng.uniform(-25,25));y=float(y0+rng.uniform(-25,25));z,gx,gy,slope=survey(x,y)
        if z<280 or not clear(x,y,10) or not river_clear(x,y,30):continue
        if .14<slope<.6 and rng.random()<.3:
            scale=float(rng.uniform(1.3,3.8));mesh=new+str(rng.choice(['rock_boulder_b2','rock_boulder_b3']));kind='Boulder';sink=.24;size=[scale,scale,scale]
        else:continue
        rock_candidates.append(dict(kind=kind,mesh=mesh,position_m=[x,y,z],yaw=math.degrees(math.atan2(gy,gx))+90+float(rng.uniform(-18,18)),scale=[float(q) for q in size],sink_fraction=sink))
if rock_candidates:
    cx=np.array([q['position_m'][0] for q in rock_candidates]);cy=np.array([q['position_m'][1] for q in rock_candidates]);clearance,_=B.exclusion_fields(cx,cy,plan,layout)
    instances.extend(q for q,c in zip(rock_candidates,clearance) if c>.99)
chunks=collections.defaultdict(list)
for p in instances:
    x,y,z=p['position_m'];chunks[f'{int(x//256)}_{int(y//256)}'].append(p)
result=dict(schema=1,pass_id='Environment.NaturalDiversity.v1',seed=103180,height_sha256=terrain.manifest['terrain_profile']['sha256'],height_modified=False,status='PLANNED',native_tag='ProjectJ.Starter.NaturalDiversity.v1',chunk_m=256,preserve_existing_tree_instances=69692,counts=dict(collections.Counter(q['kind'] for q in instances)),species_counts=dict(collections.Counter(q['mesh'] for q in instances)),chunks=dict(chunks),description='Lowland canopy preserved; mixed foothills, montane spruce, exposed alpine ridges and cliff bands. Groundcover remains LandscapeGrass-owned.')
result['mesh_material_overrides']=montane['material_overrides']
result['forest_hlod_layer']=json.loads((SRC/'StarterWorld.forest-streaming.json').read_text())['forest_hlod_layer']
FILE.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps(dict(counts=result['counts'],chunks=len(chunks),species=len(result['species_counts']))))
