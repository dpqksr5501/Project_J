"""Author dry H04 encounter approaches while retaining the unfinished lake contract."""
import importlib.util,json,math,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/MainWorld/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';REPORT=OUT/'H04-hunting-layout.json';assert not REPORT.exists()
def module(name,file):
    s=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'MainWorld.manifest.json');routes=module('routes','Starter-HuntingRoutes.py');pre=json.loads((OUT/'H04-hunting-preflight.json').read_text());survey=json.loads((OUT/'H04-hunting-site-survey.json').read_text());shore=json.loads((OUT/'H04-lake-shore-inspection.json').read_text());plan=json.loads((SOURCE/'MainWorld.plan.json').read_text());place=pre['place'];f=SOURCE/T.manifest['hunting_profiles'];data=json.loads(f.read_text());before=json.loads(json.dumps(data));assert not any(p['stable_id']=='H04' for p in data['hunting_grounds']);assert len(survey['selected'])==3 and len(shore['wet_samples'])==19 and shore['ellipse']==plan['lake']
clusters=[dict(q,id='H04.G%02d'%(i+1),role=role,reset_radius_m=30,safe_exclusion_radius_m=40,max_concurrent_candidate=3,respawn_candidate_seconds=150,respawn_policy_status='AUTHORING_CANDIDATE_NOT_ACTIVE') for i,(q,role) in enumerate(zip(survey['selected'],['SouthernShoreApproach','EasternBankShoulder','WesternReedApproach']))]
native=[v for v in pre['walking_mesh_bounds'] if v['actor']=='MainWorld_LakePrototype'];assert len(native)==1
obstacles=[v for v in pre['walking_mesh_bounds'] if v not in native];ellipse=dict(plan['lake'],margin_m=20)
R=routes.HuntingRoutes(T,place['boundary_m'],obstacles,clusters,width_m=2.8,water_ellipses=[ellipse])
def entry(preferred):
    choices=[]
    for dx in range(-40,41,2):
        for dy in range(-40,41,2):
            q=[preferred[0]+dx,preferred[1]+dy]
            if R.clear(*q) and all(math.dist(q,c['center_m'][:2])>c['safe_exclusion_radius_m']+2 for c in clusters):choices.append((math.hypot(dx,dy),q))
    assert choices,('No dry entry',preferred);return min(choices)[1]
west=entry([1320,4140]);east=entry([1860,4320]);safe=R.solve(west,east,True);trails=[dict(id='H04.DryBankPassage',role='safe_dry_shore_approach',width_m=2.8,points_m=safe)]
for q in clusters:
    x,y=q['center_m'][:2];start=min(safe,key=lambda p:math.dist(p,[x,y]));trail=dict(id=q['id']+'.Approach',role='dry_combat_branch',cluster_id=q['id'],width_m=2.4,points_m=R.solve(start,[x,y]));trails.append(trail);q['approach_trail_id']=q['retreat_trail_id']=trail['id'];q['spawn_candidates']=[dict(id=q['id']+'.Spawn%02d'%i,position_m=[x+dx,y+dy,T.height(x+dx,y+dy)],clearance_radius_m=.6,capsule_half_height_m=1,capsule_ground_gap_m=.15,terrain_validated=True,collision_validated=False,nav_validated=False) for i,(dx,dy) in enumerate([[-7,-4],[7,-4],[0,8]],1)];q['patrol_candidates_m']=[[x+dx,y+dy,T.height(x+dx,y+dy)] for dx,dy in [[-11,-7],[11,-7],[0,12]]]
props=[]
def path_distance(q,points):
    result=1e9
    for a,b in zip(points,points[1:]):
        dx,dy=b[0]-a[0],b[1]-a[1];t=max(0,min(1,((q[0]-a[0])*dx+(q[1]-a[1])*dy)/(dx*dx+dy*dy)));result=min(result,math.hypot(q[0]-a[0]-dx*t,q[1]-a[1]-dy*t))
    return result
for g in clusters:
    for i,mesh in enumerate(['/Game/Map_Resource/Megascans/3D_Assets/TreeStumpA/SM_TreeStumpA_00','/Game/Map_Resource/Brushify/Materials/Landscape/ProceduralMeshes/Rocks/Rock_02/S_Rock_02'],1):
        choices=[]
        for angle in range(0,360,15):
            a=math.radians(angle);x,y=g['center_m'][0]+math.cos(a)*26,g['center_m'][1]+math.sin(a)*26
            if R.clear(x,y) and min(path_distance([x,y],t['points_m']) for t in trails)>5 and all(math.dist([x,y],p['position_xy_m'])>4 for p in props):choices.append((angle,x,y))
        assert choices;angle,x,y=choices[(i*3)%len(choices)];props.append(dict(id=g['id']+'.Landmark%02d'%i,mesh=mesh,position_xy_m=[x,y],yaw_degrees=angle+17,scale=[1,1,1],collision=True))
branch=trails[1]['points_m'];j=safe.index(branch[0]);walk=safe[:j+1]+branch[1:]+list(reversed(branch[:-1]))+safe[j+1:]
p=dict(stable_id='H04',target_id='H04',region_id=plan['region_id'],display_name='Southern Lakeshore Approaches',archetype='dry_shore_shoulders_with_separate_combat_clearings',seed=104204,generation_version=1,layout_revision=1,stage='PLANNED',center_source='MainWorld.plan.json#places/H04',boundary_source='MainWorld.plan.json#places/H04/boundary_m',levels=place['levels'],risk_tier='MID_ENTRY',clusters=clusters,trails=trails,props=props,entries=[dict(id='H04.Entry.West',position_m=[*west,T.height(*west)],role='west_dry_approach'),dict(id='H04.Exit.East',position_m=[*east,T.height(*east)],role='east_dry_retreat')],safe_buffer_m=22,spawn_exclusion=dict(settlement_radius_margin_m=25,road_margin_m=15,water_margin_m=25,structures='Native simple/complex capsule verification',terrain_max_slope=.17),water_separation=dict(ellipse_source='MainWorld.plan.json#lake',ellipse=plan['lake'],trail_minimum_clearance_m=20,combat_minimum_clearance_m=25,native_inspection='H04-lake-shore-inspection.json',natural_shore_complete=False,water_complete=False,shore_followup='Replace floating opaque prototype edge with authored natural banks and water intersection during Phase 2. No water traversal/swimming approval.'),server_authority=dict(policy='SERVER_ONLY',npc_contract='/Script/Project_JCharacter.Project_JNPCCharacter',status='NOT_BOUND',mass_spawner_used=False,reward_writer_added=False),quest_hooks=[dict(id='H04.ShoreTracks',status='AUTHORING_HOOK_ONLY',cluster_ids=[q['id'] for q in clusters])],reset_policy=dict(status='CANDIDATE_ONLY',leash_scope='30m dry clearing candidate; safe passage centerline outside40m',ownership='Existing server AI/gameplay integration pending'),surface_extent_m=400,functional_check_points_m=walk,functional_check_scope='Complete west-to-east dry approach and southern combat branch out and back; other two branches unwalked. Unfinished water is excluded.',functional_check_timeout_s=max(300,math.ceil(sum(math.dist(a,b) for a,b in zip(walk,walk[1:]))/2+60)),terrain_policy='Preserve original heights and hills; dry approaches only. Natural waterline remediation pending, no raised blanket or flattening.',ground_query_mode='forest_floor_with_headroom',trail_headroom_check=True,art_complete=False,navigation_verified=False,gameplay_ready=False)
backup=ROOT/'Saved/StarterWorld/BeforeLakeshoreHunt';backup.mkdir(parents=True,exist_ok=True);assert not (backup/f.name).exists();shutil.copy2(f,backup/f.name);data['hunting_grounds'].append(p);assert data['hunting_grounds'][:-1]==before['hunting_grounds'];f.write_text(json.dumps(data,indent=2),encoding='utf-8');REPORT.write_text(json.dumps(dict(target_id='H04',clusters=clusters,entries=p['entries'],native_water_inspection=shore['actor_path'],natural_shore_complete=False,water_modified=False,native_blocking_meshes=len(obstacles),walk_length_m=sum(math.dist(a,b) for a,b in zip(walk,walk[1:])),height_sha256=T.manifest['terrain_profile']['sha256'],forest_modified=False,height_modified=False),indent=2),encoding='utf-8');print('H04 planned;',len(walk),'waypoints;',round(sum(math.dist(a,b) for a,b in zip(walk,walk[1:]))),'m')
