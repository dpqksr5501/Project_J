"""H05: connected forest clearings, retained heights and a scoped authored canopy."""
import importlib.util,json,math,random,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/MainWorld/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';REPORT=OUT/'H05-hunting-layout.json';assert not REPORT.exists()
def module(name,file):
    s=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'MainWorld.manifest.json');routes=module('routes','Starter-HuntingRoutes.py');pre=json.loads((OUT/'H05-hunting-preflight.json').read_text());survey=json.loads((OUT/'H05-hunting-site-survey.json').read_text());assets=json.loads((OUT/'H05-ruins-asset-preflight.json').read_text());plan=json.loads((SOURCE/'MainWorld.plan.json').read_text());place=pre['place'];f=SOURCE/T.manifest['hunting_profiles'];data=json.loads(f.read_text());before=json.loads(json.dumps(data));assert not any(p['stable_id']=='H05' for p in data['hunting_grounds']);assert len(survey['selected'])==3
clusters=[dict(q,id='H05.G%02d'%(i+1),role=role,reset_radius_m=35,safe_exclusion_radius_m=45,max_concurrent_candidate=3,respawn_candidate_seconds=180,respawn_policy_status='AUTHORING_CANDIDATE_NOT_ACTIVE') for i,(q,role) in enumerate(zip(survey['selected'],['WoodcuttersGlade','RuinCourt','UpperGrove']))]
obstacles=list(pre['walking_mesh_bounds']);shore=[]
for river in plan['rivers']:
    for a,b in zip(river['points'],river['points'][1:]):
        n=max(1,math.ceil(math.dist(a[:2],b[:2])/8))
        for i in range(n):
            x,y=a[0]+(b[0]-a[0])*i/n,a[1]+(b[1]-a[1])*i/n
            if math.dist([x,y],place['center_m'][:2])<500:shore.append(dict(center_m=[x,y,0],radius_m=river['width_m']/2+12))
obstacles+=shore;R=routes.HuntingRoutes(T,place['boundary_m'],obstacles,clusters,width_m=2.8)
def entry(preferred):
    choices=[]
    for dx in range(-40,41,2):
        for dy in range(-40,41,2):
            q=[preferred[0]+dx,preferred[1]+dy]
            if R.clear(*q) and all(math.dist(q,c['center_m'][:2])>c['safe_exclusion_radius_m']+2 for c in clusters):choices.append((math.hypot(dx,dy),q))
    assert choices,('No entry',preferred);return min(choices)[1]
west=entry([5980,5050]);north=entry([6300,5260]);safe=R.solve(west,north,True);trails=[dict(id='H05.ForestPassage',role='safe_logging_forest_passage',width_m=2.8,points_m=safe)]
for q in clusters:
    x,y=q['center_m'][:2];start=min(safe,key=lambda p:math.dist(p,[x,y]));trail=dict(id=q['id']+'.Approach',role='forest_combat_branch',cluster_id=q['id'],width_m=2.4,points_m=R.solve(start,[x,y]));trails.append(trail);q['approach_trail_id']=q['retreat_trail_id']=trail['id'];q['spawn_candidates']=[dict(id=q['id']+'.Spawn%02d'%i,position_m=[x+dx,y+dy,T.height(x+dx,y+dy)],clearance_radius_m=.6,capsule_half_height_m=1,capsule_ground_gap_m=.15,terrain_validated=True,collision_validated=False,nav_validated=False) for i,(dx,dy) in enumerate([[-7,-4],[7,-4],[0,8]],1)];q['patrol_candidates_m']=[[x+dx,y+dy,T.height(x+dx,y+dy)] for dx,dy in [[-11,-7],[11,-7],[0,12]]]
def path_distance(q,points):
    result=1e9
    for a,b in zip(points,points[1:]):
        dx,dy=b[0]-a[0],b[1]-a[1];t=max(0,min(1,((q[0]-a[0])*dx+(q[1]-a[1])*dy)/(dx*dx+dy*dy)));result=min(result,math.hypot(q[0]-a[0]-dx*t,q[1]-a[1]-dy*t))
    return result
props=[];pillar=assets['meshes'][0]['path'];facade=assets['meshes'][1]['path'];stump='/Game/Map_Resource/Megascans/3D_Assets/TreeStumpA/SM_TreeStumpA_00';wood='/Game/Map_Resource/SkyTown/Meshes/SM_Firewood01_Item01'
for g,meshes in zip(clusters,[[stump,wood,stump],[facade,facade,pillar,pillar,pillar,pillar],[stump,wood,stump]]):
    for i,mesh in enumerate(meshes,1):
        choices=[]
        for angle in range(0,360,10):
            a=math.radians(angle);x,y=g['center_m'][0]+math.cos(a)*27,g['center_m'][1]+math.sin(a)*27
            if R.clear(x,y) and min(path_distance([x,y],t['points_m']) for t in trails)>7 and all(math.dist([x,y],p['position_xy_m'])>5 for p in props):choices.append((angle,x,y))
        assert choices;angle,x,y=choices[(i*3)%len(choices)];props.append(dict(id=g['id']+'.Landmark%02d'%i,mesh=mesh,position_xy_m=[x,y],yaw_degrees=angle+90,scale=[1,1,1],collision=True,role='broken_ruin_fragment' if mesh in [pillar,facade] else 'logging_remnant'))
# Fill only the existing PCG-reserved inner core; the PCG exclusion remains its
# single authority outside this authored patch. No global forest regeneration.
rng=random.Random(105205);trees=[];cx,cy=place['center_m'][:2];tree_assets=assets['meshes'][3:];existing=[v for v in pre['forest'] if 'BlackAlder' in v['mesh']]
for attempt in range(14000):
    x,y=cx+rng.uniform(-194,194),cy+rng.uniform(-194,194)
    if math.hypot(x-cx,y-cy)>194 or not R.clear(x,y):continue
    if any(math.dist([x,y],q['center_m'][:2])<q['radius_m']+10 for q in clusters):continue
    if any(math.dist([x,y],q['center_m'][:2])<q['radius_m']+15 for q in plan['places'] if q['id'].startswith('S')):continue
    if min(path_distance([x,y],t['points_m']) for t in trails)<7:continue
    if any(math.dist([x,y],v['position_xy_m'])<8 for v in props):continue
    if any(math.dist([x,y],v['position_m'][:2])<10 for v in existing+trees):continue
    slope=math.hypot((T.height(x+1,y)-T.height(x-1,y))/2,(T.height(x,y+1)-T.height(x,y-1))/2)
    if slope>.65:continue
    variant=0 if rng.random()<.65 else rng.choice([1,2]);trees.append(dict(position_m=[x,y,T.height(x,y)-.035],yaw_degrees=rng.uniform(0,360),scale=rng.uniform(.85,1.15),variant=variant))
    if len(trees)==260:break
assert len(trees)>=150,('Insufficient scoped forest',len(trees))
sets=[dict(component='Canopy%02d'%i,mesh=mesh['path'],instances=[dict(position_m=q['position_m'],yaw_degrees=q['yaw_degrees'],scale=[q['scale']]*3) for q in trees if q['variant']==i]) for i,mesh in enumerate(tree_assets)]
group=dict(id='H05.AuthoredCanopy',center_m=place['center_m'],collision=True,sets=sets,role='scoped_forest_core',pcg_ownership='Inside 194m, existing H05 PCG tree exclusion radius204m retained. Future PCG migration must retire this owner atomically.',minimum_tree_spacing_m=10,trail_centerline_clearance_m=7)
branch=trails[1]['points_m'];j=safe.index(branch[0]);walk=safe[:j+1]+branch[1:]+list(reversed(branch[:-1]))+safe[j+1:]
p=dict(stable_id='H05',target_id='H05',region_id=plan['region_id'],display_name='Logging Forest and Broken Shrine',archetype='connected_logging_glades_and_ruin_court_in_authored_forest',seed=105205,generation_version=1,layout_revision=1,stage='PLANNED',center_source='MainWorld.plan.json#places/H05',boundary_source='MainWorld.plan.json#places/H05/boundary_m',levels=place['levels'],risk_tier='MID_FOREST',clusters=clusters,trails=trails,props=props,instance_groups=[group],entries=[dict(id='H05.Entry.West',position_m=[*west,T.height(*west)],role='west_logging_approach'),dict(id='H05.Exit.North',position_m=[*north,T.height(*north)],role='north_forest_retreat')],safe_buffer_m=27,spawn_exclusion=dict(settlement_radius_margin_m=25,road_margin_m=15,water_margin_m=25,structures='Native simple/complex capsule verification',terrain_max_slope=.17),server_authority=dict(policy='SERVER_ONLY',npc_contract='/Script/Project_JCharacter.Project_JNPCCharacter',status='NOT_BOUND',mass_spawner_used=False,reward_writer_added=False),quest_hooks=[dict(id='H05.RuinedShrineTracks',status='AUTHORING_HOOK_ONLY',cluster_ids=[q['id'] for q in clusters])],reset_policy=dict(status='CANDIDATE_ONLY',leash_scope='35m clearing candidate; safe passage outside45m',ownership='Existing server AI/gameplay integration pending'),surface_extent_m=400,functional_check_points_m=walk,functional_check_scope='West logging approach, woodcutters glade branch out/back and complete north forest passage; ruin and upper grove branches unwalked',functional_check_timeout_s=max(300,math.ceil(sum(math.dist(a,b) for a,b in zip(walk,walk[1:]))/2+60)),terrain_policy='Preserve original mountains, local elevation differences and existing trees; bounded additive forest inside reserved core only',ground_query_mode='forest_floor_with_headroom',trail_headroom_check=True,art_complete=False,navigation_verified=False,gameplay_ready=False)
backup=ROOT/'Saved/StarterWorld/BeforeLoggingRuins';backup.mkdir(parents=True,exist_ok=True);assert not (backup/f.name).exists();shutil.copy2(f,backup/f.name);data['hunting_grounds'].append(p);assert data['hunting_grounds'][:-1]==before['hunting_grounds'];f.write_text(json.dumps(data,indent=2),encoding='utf-8');REPORT.write_text(json.dumps(dict(target_id='H05',clusters=clusters,entries=p['entries'],shore_obstacles=len(shore),new_canopy_instances=len(trees),pcg_regenerated=False,walk_length_m=sum(math.dist(a,b) for a,b in zip(walk,walk[1:])),height_sha256=T.manifest['terrain_profile']['sha256'],existing_forest_modified=False,height_modified=False),indent=2),encoding='utf-8');print('H05 planned;',len(trees),'trees;',len(walk),'waypoints;',round(sum(math.dist(a,b) for a,b in zip(walk,walk[1:]))),'m')
