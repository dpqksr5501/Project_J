"""H07: sea-backed camps, a real rock bluff and a separate inland bypass."""
import importlib.util,json,math,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/MainWorld/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';REPORT=OUT/'H07-hunting-layout.json';assert not REPORT.exists()
def module(name,file):
    s=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'MainWorld.manifest.json');routes=module('routes','Starter-HuntingRoutes.py');W=module('water','Starter-WetlandGeometry.py');support=module('support','Starter-SettlementSupport.py');pre=json.loads((OUT/'H07-hunting-preflight.json').read_text());survey=json.loads((OUT/'H07-hunting-site-survey.json').read_text());plan=json.loads((SOURCE/'MainWorld.plan.json').read_text());place=pre['place'];f=SOURCE/T.manifest['hunting_profiles'];data=json.loads(f.read_text());before=json.loads(json.dumps(data));assert not any(p['stable_id']=='H07' for p in data['hunting_grounds']);assert len(survey['selected'])==3
clusters=[dict(q,id='H07.G%02d'%(i+1),role=role,reset_radius_m=32,safe_exclusion_radius_m=42,max_concurrent_candidate=3,respawn_candidate_seconds=210,respawn_policy_status='AUTHORING_CANDIDATE_NOT_ACTIVE') for i,(q,role) in enumerate(zip(survey['selected'],['EastSmugglerCamp','WestShorePatrol','UpperLookout']))]
cliff=dict(id='H07.RockBluff',mesh='/Game/Map_Resource/Brushify/Meshes/Cliffs/Meshes/Cliff_Large_01/S_Cliff_Large_01',position_xy_m=[7590,736],yaw_degrees=0,scale=[4,5,4],collision=True,role='coastal_cliff',vertical_anchor=dict(kind='top',height_m=43.25))
hazard=dict(id='H07.BluffHazard',prop_id=cliff['id'],center_m=[7590,736,T.height(7590,736)],radius_m=41,minimum_verified_drop_m=5,spawn_buffer_m=3,scope='Physical rock edge; cap access/AI/fall damage gameplay not approved')
structures=[];camp=clusters[0];cx,cy=camp['center_m'][:2];w,d=3.936,5.132
for name,preferred in [('SupplyCanopy',120),('WatchCanopy',15)]:
    choices=[]
    for degrees in range(preferred-30,preferred+31,5):
        for radius in [28,30,32]:
            a=math.radians(degrees);x,y=cx+math.cos(a)*radius,cy+math.sin(a)*radius
            if any(math.dist([x,y],q['center_m'][:2])<9 for q in structures):continue
            for yaw in [degrees-90,degrees,degrees-180]:
                co,si=math.cos(math.radians(yaw)),math.sin(math.radians(yaw));heights=[T.height(x+co*u-si*v,y+si*u+co*v) for u in [-w/2,0,w/2] for v in [-d/2,0,d/2]]
                if max(heights)-min(heights)>1.5:continue
                q=dict(id='H07.'+name,kind='tent',center_m=[x,y,max(heights)+.06],width=w,depth=d,yaw_degrees=yaw,foundation_bottom_m=min(heights)-.25,mesh='/Game/Map_Resource/SkyTown/Meshes/SM_Tents01_Item04',scale=1)
                try:access=support.plan_access_stairs(dict(id=q['id'],center_m=q['center_m'],depth=d,yaw=yaw),T.height)
                except AssertionError:continue
                if any(math.dist([x+si*distance,y-co*distance],[cx,cy])<camp['radius_m']+2 for distance,_,_,_ in access['pieces']):continue
                choices.append((abs(degrees-preferred)+abs(radius-30)+access['risers']*.2,q,access))
    assert choices,('No supported canopy',name);_,q,access=min(choices,key=lambda v:v[0]);q['access_plan']=access;structures.append(q)
coast=json.loads((SOURCE/T.manifest['coastal_surface_profile']).read_text());water=[];water_refs=[]
for q in coast['reaches']:
    x0,y0,x1,y1=q['clip_bounds_m']
    if x0>place['center_m'][0]+450 or x1<place['center_m'][0]-450:continue
    g=json.loads((SOURCE/q['geometry_source']).read_text());water.append(g);water_refs.append(dict(id=q['id'],geometry_source=q['geometry_source'],geometry_sha256=q['geometry_sha256']))
obstacles=list(pre['walking_mesh_bounds'])+[dict(center_m=hazard['center_m'],radius_m=hazard['radius_m'])]+[dict(center_m=q['center_m'],radius_m=math.hypot(w,d)/2+1) for q in structures]
for g in water:
    for a,b in g['shore_segments_m']:
        x,y=(a[0]+b[0])/2,(a[1]+b[1])/2
        if math.dist([x,y],place['center_m'][:2])<500:obstacles.append(dict(center_m=[x,y,0],radius_m=15))
# Source terrain below the sea is not a walkable combat approach.
class DryTerrain:
    def height(self,x,y):return T.height(x,y)
R=routes.HuntingRoutes(DryTerrain(),place['boundary_m'],obstacles,clusters,width_m=2.8)
base_clear=R.clear
def dry_clear(x,y):return T.height(x,y)>2 and base_clear(x,y)
R.clear=dry_clear
def entry(preferred):
    choices=[]
    for dx in range(-40,41,2):
        for dy in range(-40,41,2):
            q=[preferred[0]+dx,preferred[1]+dy]
            if R.clear(*q) and all(math.dist(q,c['center_m'][:2])>c['safe_exclusion_radius_m']+2 for c in clusters):choices.append((math.hypot(dx,dy),q))
    assert choices,('No dry entry',preferred);return min(choices)[1]
west=entry([7230,800]);east=entry([7770,840]);safe=R.solve(west,east,True);trails=[dict(id='H07.InlandBypass',role='safe_inland_alternative_away_from_bluff',width_m=2.8,points_m=safe)]
for q in clusters:
    x,y=q['center_m'][:2];start=min(safe,key=lambda p:math.dist(p,[x,y]));trail=dict(id=q['id']+'.Approach',role='coastal_combat_branch',cluster_id=q['id'],width_m=2.4,points_m=R.solve(start,[x,y]));trails.append(trail);q['approach_trail_id']=q['retreat_trail_id']=trail['id'];q['spawn_candidates']=[dict(id=q['id']+'.Spawn%02d'%i,position_m=[x+dx,y+dy,T.height(x+dx,y+dy)],clearance_radius_m=.6,capsule_half_height_m=1,capsule_ground_gap_m=.15,terrain_validated=True,collision_validated=False,nav_validated=False) for i,(dx,dy) in enumerate([[-7,-4],[7,-4],[0,8]],1)];q['patrol_candidates_m']=[[x+dx,y+dy,T.height(x+dx,y+dy)] for dx,dy in [[-11,-7],[11,-7],[0,12]]]
def path_distance(q,points):
    result=1e9
    for a,b in zip(points,points[1:]):
        dx,dy=b[0]-a[0],b[1]-a[1];t=max(0,min(1,((q[0]-a[0])*dx+(q[1]-a[1])*dy)/(dx*dx+dy*dy)));result=min(result,math.hypot(q[0]-a[0]-dx*t,q[1]-a[1]-dy*t))
    return result
props=[cliff]
for g,meshes in zip(clusters,[['/Game/Map_Resource/SkyTown/Meshes/SM_FruitCrate01','/Game/Map_Resource/SkyTown/Meshes/SM_Barrel03']*3,['/Game/Map_Resource/Brushify/Materials/Landscape/ProceduralMeshes/Rocks/Rock_02/S_Rock_02']*2,['/Game/Map_Resource/SkyTown/Meshes/SM_Barrel03','/Game/Map_Resource/Brushify/Materials/Landscape/ProceduralMeshes/Rocks/Rock_02/S_Rock_02']]):
    for i,mesh in enumerate(meshes,1):
        choices=[]
        for angle in range(0,360,10):
            a=math.radians(angle);x,y=g['center_m'][0]+math.cos(a)*26,g['center_m'][1]+math.sin(a)*26
            if R.clear(x,y) and min(path_distance([x,y],t['points_m']) for t in trails)>6 and all(math.dist([x,y],p['position_xy_m'])>4 for p in props):choices.append((angle,x,y))
        assert choices;angle,x,y=choices[(i*3)%len(choices)];props.append(dict(id=g['id']+'.Landmark%02d'%i,mesh=mesh,position_xy_m=[x,y],yaw_degrees=angle+17,scale=[1,1,1],collision=True))
branch=trails[1]['points_m'];j=safe.index(branch[0]);walk=safe[:j+1]+branch[1:]+list(reversed(branch[:-1]))+safe[j+1:]
p=dict(coastal_clearance=dict(minimum_land_height_m=2,trail_water_margin_m=12,combat_water_margin_m=25),stable_id='H07',target_id='H07',region_id=plan['region_id'],display_name='Smuggler Coast and Rock Bluff',archetype='low_coastal_camps_with_upper_lookout_and_inland_bypass',seed=107207,generation_version=1,layout_revision=1,stage='PLANNED',center_source='MainWorld.plan.json#places/H07',boundary_source='MainWorld.plan.json#places/H07/boundary_m',levels=place['levels'],risk_tier='COASTAL_FALL_HAZARD',clusters=clusters,trails=trails,props=props,structures=structures,hazards=[hazard],coastal_water_references=water_refs,entries=[dict(id='H07.Entry.West',position_m=[*west,T.height(*west)],role='west_land_approach'),dict(id='H07.Exit.East',position_m=[*east,T.height(*east)],role='east_land_retreat')],safe_buffer_m=24,spawn_exclusion=dict(settlement_radius_margin_m=25,road_margin_m=15,water_margin_m=25,hazard_margin_m=3,structures='Native simple/complex capsule verification',terrain_max_slope=.17),server_authority=dict(policy='SERVER_ONLY',npc_contract='/Script/Project_JCharacter.Project_JNPCCharacter',status='NOT_BOUND',mass_spawner_used=False,reward_writer_added=False),quest_hooks=[dict(id='H07.ContrabandTrail',status='AUTHORING_HOOK_ONLY',cluster_ids=[q['id'] for q in clusters])],reset_policy=dict(status='CANDIDATE_ONLY',leash_scope='32m candidate, public bypass outside42m and rock hazard excluded',ownership='Existing server AI/gameplay integration pending'),surface_extent_m=400,functional_check_points_m=walk,functional_check_scope='Complete west-to-east inland bypass and east smuggler camp branch out/back; other branches, cliff cap access and sea traversal unverified',functional_check_timeout_s=max(300,math.ceil(sum(math.dist(a,b) for a,b in zip(walk,walk[1:]))/2+60)),terrain_policy='Keep original coast, mountains and heights. Align derived rock cap to ridge elevation; no terrain flattening.',ground_query_mode='forest_floor_with_headroom',trail_headroom_check=True,art_complete=False,navigation_verified=False,gameplay_ready=False)
backup=ROOT/'Saved/StarterWorld/BeforeSmugglerCoast';backup.mkdir(parents=True,exist_ok=True);assert not (backup/f.name).exists();shutil.copy2(f,backup/f.name);data['hunting_grounds'].append(p);assert data['hunting_grounds'][:-1]==before['hunting_grounds'];f.write_text(json.dumps(data,indent=2),encoding='utf-8');REPORT.write_text(json.dumps(dict(target_id='H07',clusters=clusters,structures=structures,hazards=p['hazards'],water_references=water_refs,walk_length_m=sum(math.dist(a,b) for a,b in zip(walk,walk[1:])),height_sha256=T.manifest['terrain_profile']['sha256'],existing_forest_modified=False,height_modified=False),indent=2),encoding='utf-8');print('H07 planned;',len(walk),'waypoints;',round(sum(math.dist(a,b) for a,b in zip(walk,walk[1:]))),'m')
