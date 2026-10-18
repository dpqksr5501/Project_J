"""Fit a ridge stockade and lowland hunting space outside the farm village."""
import importlib.util,json,math,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';REPORT=OUT/'H02-hunting-layout.json';assert not REPORT.exists()
def module(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'StarterRegion.manifest.json');support=module('support','Starter-SettlementSupport.py');routes=module('routes','Starter-HuntingRoutes.py');pre=json.loads((OUT/'H02-hunting-preflight.json').read_text());survey=json.loads((OUT/'H02-hunting-site-survey.json').read_text());plan=json.loads((SOURCE/'StarterWorld.plan.json').read_text());place=pre['place'];base=json.loads((SOURCE/'StarterWorld.hunting.json').read_text());assert not any(p['stable_id']=='H02' for p in base['hunting_grounds']);old_h01=json.loads(json.dumps(next(p for p in base['hunting_grounds'] if p['stable_id']=='H01')))
clusters=[dict(survey['selected'][i],id='H02.G%02d'%(i+1),role=role) for i,role in enumerate(['RidgeHideout','LowlandBeastGround'])];camp=clusters[0];cx,cy=camp['center_m'][:2];radius=36;gates=[dict(id='H02.Gate.South',position_m=[cx,cy-radius,T.height(cx,cy-radius)],width_m=7),dict(id='H02.Gate.East',position_m=[cx+radius,cy,T.height(cx+radius,cy)],width_m=7)];posts=[];count=math.ceil(2*math.pi*radius/.42)
for i in range(count):
    a=2*math.pi*i/count;x,y=cx+radius*math.cos(a),cy+radius*math.sin(a)
    if any(math.dist([x,y],g['position_m'][:2])<g['width_m']/2+1 for g in gates):continue
    posts.append([x,y,T.height(x,y)-.18])
stockade=dict(id='H02.Stockade',center_m=camp['center_m'],radius_m=radius,height_m=2.7,post_spacing_m=.42,post_diameter_m=.32,post_mesh='/Game/ElderboomVillage/Meshes/SM_Modular_Fence_ufflaitdw_lod3_Var1',posts_m=posts,gates=gates)
structures=[]
for name,preferred in [('CommandCanopy',135),('SupplyCanopy',50)]:
    choices=[];w,d=3.936,5.132
    for degrees in range(preferred-30,preferred+31,5):
        a=math.radians(degrees)
        for r in [29,30,31]:
            x,y=cx+math.cos(a)*r,cy+math.sin(a)*r
            if any(math.dist([x,y],q['center_m'][:2])<10 for q in structures):continue
            for yaw in [degrees-90,degrees,degrees-180]:
                co,si=math.cos(math.radians(yaw)),math.sin(math.radians(yaw));heights=[T.height(x+co*u-si*v,y+si*u+co*v) for u in [-w/2,0,w/2] for v in [-d/2,0,d/2]]
                if max(heights)-min(heights)>1.5:continue
                floor=max(heights)+.06;q=dict(id='H02.'+name,kind='tent',center_m=[x,y,floor],width=w,depth=d,yaw_degrees=yaw,foundation_bottom_m=min(heights)-.25,mesh='/Game/Map_Resource/SkyTown/Meshes/SM_Tents01_Item04',scale=1)
                try:access=support.plan_access_stairs(dict(id=q['id'],center_m=q['center_m'],depth=d,yaw=yaw),T.height)
                except AssertionError:continue
                if any(math.dist([x+si*distance,y-co*distance],[cx,cy])<camp['radius_m']+1.8 for distance,_,_,_ in access['pieces']):continue
                choices.append((abs(degrees-preferred)+abs(r-30)+access['risers']*.2,q,access))
    assert choices,('No supported camp canopy',name)
    _,q,access=min(choices,key=lambda q:q[0]);q['access_plan']=access;structures.append(q)
obstacles=list(pre['blocking_mesh_bounds'])+[dict(center_m=q,radius_m=.16) for q in posts]+[dict(center_m=q['center_m'],radius_m=math.hypot(q['width'],q['depth'])/2+1) for q in structures]
R=routes.HuntingRoutes(T,place['boundary_m'],obstacles,clusters)
def choose_entry(preferred):
    choices=[]
    for dx in range(-24,25,2):
        for dy in range(-24,25,2):
            q=[preferred[0]+dx,preferred[1]+dy]
            if R.clear(*q):choices.append((math.hypot(dx,dy),q))
    assert choices;return min(choices)[1]
south=choose_entry([2000,2420]);north=choose_entry([1940,2960]);safe=R.solve(south,north,True);trails=[dict(id='H02.SafePassage',role='safe_passage',width_m=3.2,points_m=safe)]
south_gate=gates[0]['position_m'][:2];east_gate=gates[1]['position_m'][:2]
for name,start,gate in [('SouthGate',min(safe,key=lambda q:math.dist(q,south_gate)),south_gate),('EastEscape',north,east_gate)]:
    points=R.solve(start,gate)+R.solve(gate,[cx,cy])[1:];trails.append(dict(id='H02.Camp.'+name,cluster_id=camp['id'],role='camp_approach' if name=='SouthGate' else 'alternate_camp_retreat',width_m=3,points_m=points))
beast=clusters[1];start=min(safe,key=lambda q:math.dist(q,beast['center_m'][:2]));trails.append(dict(id='H02.BeastBranch',cluster_id=beast['id'],role='combat_branch',width_m=2.8,points_m=R.solve(start,beast['center_m'][:2])))
for q in clusters:
    x,y=q['center_m'][:2];q.update(reset_radius_m=32,max_concurrent_candidate=3,respawn_candidate_seconds=120,respawn_policy_status='AUTHORING_CANDIDATE_NOT_ACTIVE',approach_trail_id=trails[1 if q==camp else 3]['id'],retreat_trail_id=trails[2 if q==camp else 3]['id']);q['spawn_candidates']=[dict(id=q['id']+'.Spawn%02d'%i,position_m=[x+dx,y+dy,T.height(x+dx,y+dy)],clearance_radius_m=.6,capsule_half_height_m=1,capsule_ground_gap_m=.15,terrain_validated=True,collision_validated=False,nav_validated=False) for i,(dx,dy) in enumerate([[-8,-5],[8,-5],[0,9]],1)];q['patrol_candidates_m']=[[x+dx,y+dy,T.height(x+dx,y+dy)] for dx,dy in [[-12,-8],[12,-8],[0,13]]]
# Walk into the actual south opening, cross the courtyard and leave through the east opening.
first=trails[1]['points_m'];second=trails[2]['points_m'];j=safe.index(first[0]);walk=safe[:j+1]+first[1:]+list(reversed(second[:-1]))
props=[]
for name,path,x,y,yaw in [('ColdFire','/Game/ElderboomVillage/Meshes/SM_Campfire_uenpfeifa_lod3_Var1',cx-27,cy-8,0),('Supplies','/Game/Map_Resource/SkyTown/Meshes/SM_Sacks01_Item01',cx-25,cy+13,30),('StolenCart','/Game/Map_Resource/SkyTown/Meshes/SM_Cart01',cx+27,cy-12,90),('DenRockA','/Game/Map_Resource/Brushify/Materials/Landscape/ProceduralMeshes/Rocks/Rock_02/S_Rock_02',beast['center_m'][0]+32,beast['center_m'][1]+20,20),('DenRockB','/Game/Map_Resource/Brushify/Materials/Landscape/ProceduralMeshes/Rocks/Rock_02/S_Rock_02',beast['center_m'][0]+36,beast['center_m'][1]+17,67)]:
    props.append(dict(id='H02.'+name,mesh=path,position_xy_m=[x,y],yaw_degrees=yaw,scale=[1,1,1],collision=True))
p=dict(stable_id='H02',target_id='H02',region_id=plan['region_id'],display_name='Farm Outskirts Hideout',archetype='ridge_stockade_and_beast_hollow',seed=103202,generation_version=1,layout_revision=1,stage='PLANNED',center_source='StarterWorld.plan.json#places/H02',boundary_source='StarterWorld.plan.json#places/H02/boundary_m',levels=place['levels'],risk_tier='LOW_MID',clusters=clusters,stockades=[stockade],structures=structures,props=props,trails=trails,entries=[dict(id='H02.Entry.FarmApproach',position_m=[*south,T.height(*south)],role='farm_side_approach'),dict(id='H02.Exit.North',position_m=[*north,T.height(*north)],role='alternate_retreat')],safe_buffer_m=20,spawn_exclusion=dict(settlement_radius_margin_m=25,road_margin_m=15,water_margin_m=25,structures='Source planned footprint + Native simple/complex capsule verification',terrain_max_slope=.17),server_authority=dict(policy='SERVER_ONLY',npc_contract='/Script/Project_JCharacter.Project_JNPCCharacter',status='NOT_BOUND',mass_spawner_used=False,reward_writer_added=False),quest_hooks=[dict(id='H02.StolenSupplies',status='AUTHORING_HOOK_ONLY',cluster_id=camp['id']),dict(id='H02.BeastTrail',status='AUTHORING_HOOK_ONLY',cluster_id=beast['id'])],reset_policy=dict(status='CANDIDATE_ONLY',leash_scope='cluster reset radius; safe passage excluded',ownership='Existing server AI/gameplay contract integration pending'),surface_extent_m=400,functional_check_points_m=walk,functional_check_scope='Farm-side local entry, south stockade gate, courtyard, east stockade gate and north retreat; beast branch/full graph unwalked',functional_check_timeout_s=max(300,math.ceil(sum(math.dist(a,b) for a,b in zip(walk,walk[1:]))/2+60)),functional_walk_points_m=walk,functional_walk_timeout_s=600,terrain_policy='Preserve original farm, source ridge and lowland; individual foundations and contour routes',art_complete=False,navigation_verified=False,gameplay_ready=False)
backup=ROOT/'Saved/StarterWorld/BeforeFarmHideout';backup.mkdir(parents=True,exist_ok=True);f=SOURCE/'StarterWorld.hunting.json';assert not (backup/f.name).exists();shutil.copy2(f,backup/f.name);base['hunting_grounds'].append(p);assert next(v for v in base['hunting_grounds'] if v['stable_id']=='H01')==old_h01;f.write_text(json.dumps(base,indent=2),encoding='utf-8');REPORT.write_text(json.dumps(dict(target_id='H02',stockade_posts=len(posts),structures=structures,clusters=clusters,walk_length_m=sum(math.dist(a,b) for a,b in zip(walk,walk[1:])),height_sha256=T.manifest['terrain_profile']['sha256'],height_modified=False),indent=2),encoding='utf-8');print('Planned H02:',len(posts),'posts;',len(structures),'canopies; local walk',REPORT)
