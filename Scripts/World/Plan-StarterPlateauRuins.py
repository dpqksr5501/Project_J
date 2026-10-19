"""H09's natural lower grove, raised shrine shelf and open ruin court."""
import hashlib,importlib.util,json,math,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/MainWorld/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';REPORT=OUT/'H09-hunting-layout.json';assert not REPORT.exists()
def module(name,file):
    s=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'MainWorld.manifest.json');routes=module('routes','Starter-HuntingRoutes.py');PRE=json.loads((OUT/'H09-hunting-preflight.json').read_text());ASSETS={m['path'].replace('/Game/AncientTempleRuins/Environment/','/Game/Map_Resource/AncientTempleRuins/Environment/',1):m for m in json.loads((OUT/'H09-ruins-asset-preflight.json').read_text())['meshes']};plan=json.loads((SOURCE/'MainWorld.plan.json').read_text());place=PRE['place'];base='/Game/Map_Resource/AncientTempleRuins/Environment/Construction/';tall=base+'Pillar/SM_Pillar_01_128_128_512_01';short=base+'Pillar/SM_Pillar_01_64_64_256_D';wall=base+'Wall/SM_Wall_01_Linear_512_512_01_A';arch=base+'Wall/SM_Wall_01_Open_192_256_01_A';floor=base+'Floor/SM_Floor_Engraved_800_800_01_A'
clusters=[]
for id,role,x,y,r in [('Court','upper_ruin_court',6420,6270,18),('Shrine','raised_elite_shrine_shelf',6420,6230,10),('Grove','lower_wooded_patrol',6420,6030,10)]:
    clusters.append(dict(id='H09.'+id,role=role,center_m=[x,y,T.height(x,y)],radius_m=r,reset_radius_m=r+12,safe_exclusion_radius_m=r+18,max_concurrent_candidate=3 if r>10 else 2,respawn_candidate_seconds=270,respawn_policy_status='AUTHORING_CANDIDATE_NOT_ACTIVE'))
props=[]
def prop(name,mesh,x,y,yaw=0,role='broken_ruin_fragment'):
    assert mesh in ASSETS;props.append(dict(id='H09.'+name,mesh=mesh,position_xy_m=[x,y],yaw_degrees=yaw,scale=[1,1,1],collision=True,role=role))
for i,(x,y) in enumerate([(6396,6270),(6408,6291),(6432,6291),(6444,6270)],1):prop('CourtPillar%02d'%i,tall,x,y,17*i)
prop('NorthArch',arch,6420,6296,0,'ruin_arch_frame');prop('WestCourtWall',wall,6396,6283,90);prop('EastCourtWall',wall,6446,6282,90)
prop('ShrinePillarWest',short,6405,6230,0);prop('ShrinePillarEast',short,6435,6230,35);prop('ShrineBackWall',wall,6420,6206,0);prop('BuriedEngravedSlab',floor,6420,6212,0,'buried_relic_slab_outside_combat_and_routes')
def prop_radius(p):
    m=ASSETS[p['mesh']];return math.hypot(m['extent_cm'][0],m['extent_cm'][1])/100
for name,preferred in [('GrovePillarWest',180),('GrovePillarEast',0)]:
    options=[]
    for a in range(0,360,10):
        x,y=6420+math.cos(math.radians(a))*16,6030+math.sin(math.radians(a))*16
        if any(math.dist([x,y],o['center_m'][:2])<o['radius_m']+2 for o in PRE['walking_mesh_bounds']):continue
        h=[T.height(x+u,y+v) for u in [-1,0,1] for v in [-1,0,1]]
        if max(h)-min(h)>1.5:continue
        if any(math.dist([x,y],p['position_xy_m'])<5 for p in props):continue
        options.append((abs((a-preferred+180)%360-180),x,y,a))
    assert options,('No terrain-preserving lower ruin fragment position',name);_,x,y,a=min(options);prop(name,short,x,y,a)
obstacles=PRE['walking_mesh_bounds']+[dict(center_m=[*p['position_xy_m'],T.height(*p['position_xy_m'])],radius_m=prop_radius(p)+.5) for p in props]
R=routes.HuntingRoutes(T,place['boundary_m'],obstacles,clusters,width_m=2.8,grade_limit=.18);clear=R.clear
def walkable_clear(x,y):
    return clear(x,y) and math.hypot((T.height(x+.5,y)-T.height(x-.5,y)),(T.height(x,y+.5)-T.height(x,y-.5)))<.9
R.clear=walkable_clear
def entry(preferred):
    options=[]
    for dx in range(-40,41,2):
        for dy in range(-40,41,2):
            q=[preferred[0]+dx,preferred[1]+dy]
            if R.clear(*q) and all(math.dist(q,p['center_m'][:2])>p['safe_exclusion_radius_m']+2 for p in clusters):options.append((math.hypot(dx,dy),q))
    assert options,('No clear entry',preferred);return min(options)[1]
south=entry([6360,6010]);north=entry([6380,6330]);safe=R.solve(south,north,True);trails=[dict(id='H09.WesternBypass',role='safe_western_contour_bypass',width_m=2.8,points_m=safe)];branches=[]
for q in clusters:
    x,y=q['center_m'][:2];start=min(safe,key=lambda p:math.dist(p,[x,y]));points=R.solve(start,[x,y]);trail=dict(id=q['id']+'.Approach',role='ruin_combat_branch_and_retreat',cluster_id=q['id'],width_m=2.4,points_m=points);trails.append(trail);branches.append((safe.index(start),points));q['approach_trail_id']=q['retreat_trail_id']=trail['id'];small=q['radius_m']<=10;offsets=[[-3,-3],[3,3]] if small else [[-7,-4],[7,-4],[0,8]];q['spawn_candidates']=[dict(id=q['id']+'.Spawn%02d'%i,position_m=[x+dx,y+dy,T.height(x+dx,y+dy)],clearance_radius_m=.6,capsule_half_height_m=1,capsule_ground_gap_m=.15,terrain_validated=True,collision_validated=False,nav_validated=False) for i,(dx,dy) in enumerate(offsets,1)];q['patrol_candidates_m']=[[x+dx,y+dy,T.height(x+dx,y+dy)] for dx,dy in ([[-4,2],[4,-2]] if small else [[-10,-6],[10,-6],[0,11]])]
walk=[]
for i,p in enumerate(safe):
    walk.append(p)
    for j,branch in branches:
        if i==j:walk.extend(branch[1:]);walk.extend(list(reversed(branch[:-1])))
P=dict(stable_id='H09',target_id='H09',region_id=plan['region_id'],display_name='Plateau Ruin Court and Lower Grove',archetype='tiered_ruins_on_natural_shelves_with_western_bypass',seed=109209,generation_version=1,layout_revision=1,stage='PLANNED',center_source='MainWorld.plan.json#places/H09',boundary_source='MainWorld.plan.json#places/H09/boundary_m',levels=place['levels'],risk_tier='ELITE_RUINS_AND_HEIGHT_CHANGE',clusters=clusters,trails=trails,props=props,structures=[],entries=[dict(id='H09.Entry.SouthGrove',position_m=[*south,T.height(*south)],role='southern_forest_approach'),dict(id='H09.Retreat.North',position_m=[*north,T.height(*north)],role='northwestern_highland_retreat')],safe_buffer_m=18,spawn_exclusion=dict(settlement_radius_margin_m=25,public_road_margin_m=15,structures='Actual imported mesh bounds and native simple/complex visibility capsules'),server_authority=dict(policy='SERVER_ONLY',npc_contract='/Script/Project_JCharacter.Project_JNPCCharacter',status='NOT_BOUND',mass_spawner_used=False,reward_writer_added=False),quest_hooks=[dict(id='H09.RuinedObservatory',status='AUTHORING_HOOK_ONLY',cluster_ids=[p['id'] for p in clusters])],reset_policy=dict(status='CANDIDATE_ONLY',leash_scope='Separate lower grove, raised shrine and broad court; west bypass outside candidate cores',ownership='Existing server AI/gameplay integration pending'),surface_extent_m=420,functional_check_points_m=walk,functional_check_scope='Complete western bypass and all three ruin branches out/back; free exploration, mounts and regional roads unverified',functional_check_timeout_s=max(480,math.ceil(sum(math.dist(a,b) for a,b in zip(walk,walk[1:]))/1.1+90)),terrain_policy='Keep original highland ridges, natural lower grove and raised shelves. No terrain flattening.',ground_query_mode='forest_floor_with_headroom',trail_headroom_check=True,art_complete=False,navigation_verified=False,gameplay_ready=False)
f=SOURCE/T.manifest['hunting_profiles'];data=json.loads(f.read_text());assert not any(p['target_id']=='H09' for p in data['hunting_grounds']);backup=ROOT/'Saved/StarterWorld/BeforePlateauRuins';backup.mkdir(parents=True,exist_ok=False);shutil.copy2(f,backup/f.name);data['hunting_grounds'].append(P);f.write_text(json.dumps(data,indent=2),encoding='utf-8');REPORT.write_text(json.dumps(dict(target_id='H09',clusters=clusters,props=props,walk_points=len(walk),walk_length_m=sum(math.dist(a,b) for a,b in zip(walk,walk[1:])),height_modified=False,existing_forest_modified=False),indent=2),encoding='utf-8');print('H09 planned',len(walk),'points;',len(props),'ruin props')
