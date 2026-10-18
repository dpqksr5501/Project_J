"""Extend the existing S00 village with a roadside watch, preserving its layout."""
import importlib.util,json,math,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';FILE=OUT/'road-watch-layout.json';assert not FILE.exists()
def module(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'StarterRegion.manifest.json');support=module('support','Starter-SettlementSupport.py');f=SOURCE/'StarterWorld.settlements.json';data=json.loads(f.read_text());assert not any(v['target_id']=='S08' for v in data['settlements']);plan=json.loads((SOURCE/'StarterWorld.plan.json').read_text());place=next(v for v in plan['places'] if v['id']=='S00');cx,cy=place['center_m'][:2];old=json.loads((SOURCE/'StarterWorld.corridor.json').read_text())
barracks=dict(id='RoadBarracks',offset_m=[30,114],width=6,depth=8,storeys=1,yaw_degrees=0)
lookout=dict(offset_m=[60,120],width_m=5,depth_m=5,yaw_degrees=0,deck_raise_m=5.4)
lx,ly=cx+60,cy+120;floor=max(T.height(lx+u,ly+v) for u in [-2.5,0,2.5] for v in [-2.5,0,2.5])+5.4;lookout['deck_top_m']=floor;stairs=support.plan_access_stairs(dict(id='RoadWatch',center_m=[lx,ly,floor],yaw=0,depth=5),T.height);lookout['approach_m']=[lx,ly-2.5-stairs['landing_depth_m']-stairs['risers']*stairs['tread_run_m']-.4]
checkpoint=dict(center_m=[3158,3180,T.height(3158,3180)],heading_degrees=math.degrees(math.atan2(400,290)),road_clear_width_m=6,rail_offsets_m=[-5,5],rail_length_m=8)
market=dict(id='RoadsideStall',offset_m=[85,88],width=3.936,depth=5.132,yaw_degrees=-90,mesh='/Game/Map_Resource/SkyTown/Meshes/SM_Tents01_Item04',mesh_scale=1)
for q in [barracks,market]:
    x,y=cx+q['offset_m'][0],cy+q['offset_m'][1];a=math.radians(q['yaw_degrees']);co,si=math.cos(a),math.sin(a);heights=[T.height(x+co*u-si*v,y+si*u+co*v) for u in [-q['width']/2,0,q['width']/2] for v in [-q['depth']/2,0,q['depth']/2]];assert max(heights)-min(heights)<2.5;q['center_m']=[x,y,max(heights)+.06];q['foundation_bottom_m']=min(heights)-.25
    support.plan_access_stairs(dict(id=q['id'],center_m=q['center_m'],yaw=q['yaw_degrees'],depth=q['depth']),T.height)
    assert all(math.dist([x,y],[h['x'],h['y']])>math.hypot(q['width'],q['depth'])/2+math.hypot(h['width'],h['depth'])/2+4 for h in old['houses'])
streets=[dict(id='CapitalRoadCheckpoint',width_m=6,points_relative_m=[[20,28],[58,80],[95,130]]),dict(id='WatchApproach',width_m=2.4,points_relative_m=[[58,80],[lookout['approach_m'][0]-cx,lookout['approach_m'][1]-cy],[60,120]])]
walk=[[3120,3128],[3158,3180],lookout['approach_m'],[lx,ly-3],[lx,ly]]
p=dict(stable_id='S00',target_id='S08',archetype='road_watch_starting_hamlet',seed=103108,stage='PLANNED',center_source='StarterWorld.plan.json#places/S00',existing_village_source='StarterWorld.corridor.json',houses=[barracks],tents=[market],fields=[],work_yards=[dict(id='WatchTowerBase',offset_m=[60,120],width_m=6,depth_m=6)],streets=streets,lookout=lookout,checkpoint=checkpoint,walk_route_points_m=walk,functional_walk_timeout_s=100,plaza=dict(offset_m=[58,80],radius_m=4,blend_m=2),roof_contact_profile=data['settlements'][0]['roof_contact_profile'],terrain_policy='Keep original 8-house village and source heights; append roadside structures')
backup=ROOT/'Saved/StarterWorld/BeforeRoadWatch';backup.mkdir(parents=True,exist_ok=True)
for file in [f,SOURCE/'StarterWorld.corridor.json']:assert not (backup/file.name).exists();shutil.copy2(file,backup/file.name)
data['settlements'].append(p);f.write_text(json.dumps(data,indent=2),encoding='utf-8');FILE.write_text(json.dumps(dict(profile=p,watch_access=stairs,height_sha256=T.manifest['terrain_profile']['sha256'],source_plan_modified=False,old_houses_modified=False),indent=2),encoding='utf-8');print('S08 road watch planned; deck',floor,'risers',stairs['risers'])
