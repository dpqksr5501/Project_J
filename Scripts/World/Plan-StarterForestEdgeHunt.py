"""Use existing forest-edge cover and keep dry approaches outside encounter leashes."""
import importlib.util,json,math,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';REPORT=OUT/'H03-hunting-layout.json';assert not REPORT.exists()
def module(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'StarterRegion.manifest.json');routes=module('routes','Starter-HuntingRoutes.py');pre=json.loads((OUT/'H03-hunting-preflight.json').read_text());survey=json.loads((OUT/'H03-hunting-site-survey.json').read_text());plan=json.loads((SOURCE/'StarterWorld.plan.json').read_text());place=pre['place'];f=SOURCE/T.manifest['hunting_profiles'];data=json.loads(f.read_text());before=json.loads(json.dumps(data));assert not any(p['stable_id']=='H03' for p in data['hunting_grounds'])
clusters=[dict(q,id='H03.G%02d'%(i+1),role=role,reset_radius_m=45,safe_exclusion_radius_m=65,max_concurrent_candidate=3,respawn_candidate_seconds=120,respawn_policy_status='AUTHORING_CANDIDATE_NOT_ACTIVE') for i,(q,role) in enumerate(zip(survey['selected'],['NorthGlade','SouthwestBrush','GroveBend']))];trees=[v for v in pre['forest'] if 'BlackAlder' in v['mesh']];obstacles=list(pre['walking_mesh_bounds']);shore=[]
for river in plan['rivers']:
    for a,b in zip(river['points'],river['points'][1:]):
        distance=math.dist(a[:2],b[:2]);n=max(1,math.ceil(distance/8))
        for i in range(n):
            x,y=a[0]+(b[0]-a[0])*i/n,a[1]+(b[1]-a[1])*i/n
            if math.dist([x,y],place['center_m'][:2])<500:shore.append(dict(center_m=[x,y,0],radius_m=river['width_m']/2+12))
obstacles+=shore;R=routes.HuntingRoutes(T,place['boundary_m'],obstacles,clusters,width_m=2.8)
def entry(preferred):
    choices=[]
    for dx in range(-60,61,2):
        for dy in range(-60,61,2):
            q=[preferred[0]+dx,preferred[1]+dy]
            if R.clear(*q) and all(math.dist(q,c['center_m'][:2])>c['safe_exclusion_radius_m']+2 for c in clusters):choices.append((math.hypot(dx,dy),q))
    assert choices,('No dry entry',preferred);return min(choices)[1]
west=entry([5260,3880]);north=entry([5510,4140]);safe=R.solve(west,north,True);trails=[dict(id='H03.ForestPassage',role='safe_forest_passage',width_m=2.8,points_m=safe)]
for q in clusters:
    x,y=q['center_m'][:2];start=min(safe,key=lambda p:math.dist(p,[x,y]));points=R.solve(start,[x,y]);trail=dict(id=q['id']+'.ForestBranch',role='forest_combat_branch',cluster_id=q['id'],width_m=2.4,points_m=points);trails.append(trail);q['approach_trail_id']=q['retreat_trail_id']=trail['id'];q['spawn_candidates']=[dict(id=q['id']+'.Spawn%02d'%i,position_m=[x+dx,y+dy,T.height(x+dx,y+dy)],clearance_radius_m=.6,capsule_half_height_m=1,capsule_ground_gap_m=.15,terrain_validated=True,collision_validated=False,nav_validated=False) for i,(dx,dy) in enumerate([[-7,-4],[7,-4],[0,8]],1)];q['patrol_candidates_m']=[[x+dx,y+dy,T.height(x+dx,y+dy)] for dx,dy in [[-11,-7],[11,-7],[0,12]]]
    selected=sorted(trees,key=lambda t:math.dist([x,y],t['position_m'][:2]));q['cover_references']=[]
    for i,t in enumerate(selected[:2],1):q['cover_references'].append(dict(id=q['id']+'.Cover%02d'%i,actor_label=t['actor'],component=t['component'],instance=t['index'],mesh=t['mesh'],position_m=t['position_m'],distance_from_cluster_m=math.dist([x,y],t['position_m'][:2]),status='EXISTING_TREE_COLLISION_TO_VERIFY',reference_scope='Authoring mesh/transform snapshot; refresh after intentional PCG regeneration'))
props=[]
def path_distance(q,points):
    best=1e9
    for a,b in zip(points,points[1:]):
        dx,dy=b[0]-a[0],b[1]-a[1];t=max(0,min(1,((q[0]-a[0])*dx+(q[1]-a[1])*dy)/(dx*dx+dy*dy)));best=min(best,math.hypot(q[0]-a[0]-dx*t,q[1]-a[1]-dy*t))
    return best
for g in clusters:
    for i,mesh in enumerate(['/Game/Megascans/3D_Assets/TreeStumpA/SM_TreeStumpA_00','/Game/Map_Resource/Brushify/Materials/Landscape/ProceduralMeshes/Rocks/Rock_02/S_Rock_02'],1):
        candidates=[]
        for angle in range(0,360,15):
            a=math.radians(angle);x,y=g['center_m'][0]+math.cos(a)*27,g['center_m'][1]+math.sin(a)*27
            if not R.clear(x,y) or any(math.dist([x,y],v['position_xy_m'])<4 for v in props):continue
            if min(path_distance([x,y],t['points_m']) for t in trails)<5:continue
            candidates.append((angle,x,y))
        assert candidates;angle,x,y=candidates[(i*3)%len(candidates)];props.append(dict(id=g['id']+'.Landmark%02d'%i,mesh=mesh,position_xy_m=[x,y],yaw_degrees=angle+17,scale=[1,1,1],collision=True))
# Representative traversal enters one clearing and returns to the public passage.
branch=trails[2]['points_m'];j=safe.index(branch[0]);walk=safe[:j+1]+branch[1:]+list(reversed(branch[:-1]))+safe[j+1:]
full=list(walk)
for trail in [trails[1],trails[3]]:
    path=trail['points_m'];full+=R.solve(full[-1],path[0],True)[1:]+path[1:]+list(reversed(path[:-1]))
p=dict(stable_id='H03',target_id='H03',region_id=plan['region_id'],display_name='Eastern Forest Fringe',archetype='sparse_forest_transition_with_existing_cover',seed=103203,generation_version=1,layout_revision=1,stage='PLANNED',center_source='StarterWorld.plan.json#places/H03',boundary_source='StarterWorld.plan.json#places/H03/boundary_m',levels=place['levels'],risk_tier='MID_ENTRY',clusters=clusters,trails=trails,props=props,entries=[dict(id='H03.Entry.West',position_m=[*west,T.height(*west)],role='west_forest_approach'),dict(id='H03.Exit.North',position_m=[*north,T.height(*north)],role='north_forest_retreat')],safe_buffer_m=20,spawn_exclusion=dict(settlement_radius_margin_m=25,road_margin_m=15,water_margin_m=25,structures='Native simple/complex capsule verification',terrain_max_slope=.17),server_authority=dict(policy='SERVER_ONLY',npc_contract='/Script/Project_JCharacter.Project_JNPCCharacter',status='NOT_BOUND',mass_spawner_used=False,reward_writer_added=False),quest_hooks=[dict(id='H03.ForestTracks',status='AUTHORING_HOOK_ONLY',cluster_ids=[q['id'] for q in clusters])],reset_policy=dict(status='CANDIDATE_ONLY',leash_scope='45m includes nearby tree cover; public passage centerline outside65m',ownership='Existing server AI/gameplay integration pending'),surface_extent_m=400,functional_check_points_m=walk,functional_check_scope='West entry, southwest brush branch and retreat, then complete northbound public forest passage; other branches/full graph unwalked',functional_check_timeout_s=max(300,math.ceil(sum(math.dist(a,b) for a,b in zip(walk,walk[1:]))/2+60)),functional_walk_points_m=full,functional_walk_timeout_s=math.ceil(sum(math.dist(a,b) for a,b in zip(full,full[1:]))/2+60),terrain_policy='Keep original canopy, individual trees, shrubs and source heights; natural transition gaps',art_complete=False,navigation_verified=False,gameplay_ready=False)
p.update(ground_query_mode='forest_floor_with_headroom',trail_headroom_check=True)
backup=ROOT/'Saved/StarterWorld/BeforeForestEdgeHunt';backup.mkdir(parents=True,exist_ok=True);assert not (backup/f.name).exists();shutil.copy2(f,backup/f.name);data['hunting_grounds'].append(p);assert data['hunting_grounds'][:-1]==before['hunting_grounds'];f.write_text(json.dumps(data,indent=2),encoding='utf-8');REPORT.write_text(json.dumps(dict(target_id='H03',clusters=clusters,entries=p['entries'],shore_obstacles=len(shore),native_blocking_meshes=len(pre['blocking_mesh_bounds']),walk_length_m=sum(math.dist(a,b) for a,b in zip(walk,walk[1:])),height_sha256=T.manifest['terrain_profile']['sha256'],forest_modified=False,height_modified=False),indent=2),encoding='utf-8');print('H03 planned; representative route',round(sum(math.dist(a,b) for a,b in zip(walk,walk[1:]))),'m')
