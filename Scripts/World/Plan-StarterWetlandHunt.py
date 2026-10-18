"""H06: natural shallow basins, dry benches and a shared wet-fringe input."""
import hashlib,importlib.util,json,math,random,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';REPORT=OUT/'H06-hunting-layout.json';assert not REPORT.exists()
def module(name,file):
    s=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'StarterRegion.manifest.json');routes=module('routes','Starter-HuntingRoutes.py');W=module('wetland','Starter-WetlandGeometry.py');pre=json.loads((OUT/'H06-hunting-preflight.json').read_text());survey=json.loads((OUT/'H06-hunting-site-survey.json').read_text());surface=json.loads((OUT/'H06-wetland-surface-survey.json').read_text());assets=json.loads((OUT/'H06-fringe-asset-preflight.json').read_text());plan=json.loads((SOURCE/'StarterWorld.plan.json').read_text());place=pre['place'];f=SOURCE/T.manifest['hunting_profiles'];data=json.loads(f.read_text());before=json.loads(json.dumps(data));assert not any(p['stable_id']=='H06' for p in data['hunting_grounds']);assert len(survey['selected'])==3
clusters=[dict(q,id='H06.G%02d'%(i+1),role=role,reset_radius_m=30,safe_exclusion_radius_m=40,max_concurrent_candidate=3,respawn_candidate_seconds=180,respawn_policy_status='AUTHORING_CANDIDATE_NOT_ACTIVE') for i,(q,role) in enumerate(zip(survey['selected'],['SouthDryBench','NorthDryBench','UpperRetreatKnoll']))]
obstacles=list(pre['walking_mesh_bounds'])
for pool in surface['pools']:
    assert pool['maximum_depth_m']<=1 and pool['source_height_modified']==False
    obstacles.extend(dict(center_m=q,radius_m=12) for q in pool['vertices_m'])
R=routes.HuntingRoutes(T,place['boundary_m'],obstacles,clusters,width_m=2.8)
def entry(preferred):
    choices=[]
    for dx in range(-40,41,2):
        for dy in range(-40,41,2):
            q=[preferred[0]+dx,preferred[1]+dy]
            if R.clear(*q) and all(math.dist(q,c['center_m'][:2])>c['safe_exclusion_radius_m']+2 for c in clusters):choices.append((math.hypot(dx,dy),q))
    assert choices,('No dry entry',preferred);return min(choices)[1]
south=entry([4780,1450]);north=entry([4740,1960]);safe=R.solve(south,north,True);trails=[dict(id='H06.DryBenchPassage',role='safe_wetland_land_route',width_m=2.8,points_m=safe)]
for q in clusters:
    x,y=q['center_m'][:2];start=min(safe,key=lambda p:math.dist(p,[x,y]));trail=dict(id=q['id']+'.Approach',role='dry_combat_branch',cluster_id=q['id'],width_m=2.4,points_m=R.solve(start,[x,y]));trails.append(trail);q['approach_trail_id']=q['retreat_trail_id']=trail['id'];q['spawn_candidates']=[dict(id=q['id']+'.Spawn%02d'%i,position_m=[x+dx,y+dy,T.height(x+dx,y+dy)],clearance_radius_m=.6,capsule_half_height_m=1,capsule_ground_gap_m=.15,terrain_validated=True,collision_validated=False,nav_validated=False) for i,(dx,dy) in enumerate([[-7,-4],[7,-4],[0,8]],1)];q['patrol_candidates_m']=[[x+dx,y+dy,T.height(x+dx,y+dy)] for dx,dy in [[-11,-7],[11,-7],[0,12]]]
def path_distance(q,points):
    result=1e9
    for a,b in zip(points,points[1:]):
        dx,dy=b[0]-a[0],b[1]-a[1];t=max(0,min(1,((q[0]-a[0])*dx+(q[1]-a[1])*dy)/(dx*dx+dy*dy)));result=min(result,math.hypot(q[0]-a[0]-dx*t,q[1]-a[1]-dy*t))
    return result
props=[]
for g in clusters:
    for i,mesh in enumerate(['/Game/Map_Resource/Brushify/Materials/Landscape/ProceduralMeshes/Rocks/Rock_02/S_Rock_02','/Game/Megascans/3D_Assets/TreeStumpA/SM_TreeStumpA_00'],1):
        choices=[]
        for angle in range(0,360,15):
            a=math.radians(angle);x,y=g['center_m'][0]+math.cos(a)*26,g['center_m'][1]+math.sin(a)*26
            if R.clear(x,y) and min(path_distance([x,y],t['points_m']) for t in trails)>6 and all(math.dist([x,y],p['position_xy_m'])>5 for p in props):choices.append((angle,x,y))
        assert choices;angle,x,y=choices[(i*3)%len(choices)];props.append(dict(id=g['id']+'.Landmark%02d'%i,mesh=mesh,position_xy_m=[x,y],yaw_degrees=angle+17,scale=[1,1,1],collision=True))
rng=random.Random(106206);fringe=[]
for pool in surface['pools']:
    x0,y0,x1,y1=pool['bounds_m'];local=[]
    for attempt in range(10000):
        x,y=rng.uniform(x0-8,x1+8),rng.uniform(y0-8,y1+8);distance=W.surface_distance(x,y,pool)
        # Decorative dry-bank plants use the true contour, not the route
        # solver's deliberate 12m water buffer.
        if not .3<distance<7:continue
        if T.height(x,y)-pool['water_level_m']>1.5:continue
        if any(path_distance([x,y],t['points_m'])<t['width_m']/2+2 for t in trails):continue
        if any(math.dist([x,y],v['position_m'][:2])<.8 for v in local):continue
        slope=math.hypot((T.height(x+1,y)-T.height(x-1,y))/2,(T.height(x,y+1)-T.height(x,y-1))/2)
        if slope>.6:continue
        variant=rng.choices([0,1,2],[.45,.4,.15])[0];local.append(dict(position_m=[x,y,T.height(x,y)-.035],yaw_degrees=rng.uniform(0,360),scale=[rng.uniform(.85,1.2)]*3,variant=variant))
        if len(local)==500:break
    assert len(local)>=200,(pool['id'],len(local));fringe+=local
sets=[dict(component='WetFringe%02d'%i,mesh=asset['path'],instances=[{k:v for k,v in q.items() if k!='variant'} for q in fringe if q['variant']==i]) for i,asset in enumerate(assets)]
group=dict(id='H06.WetFringe',center_m=place['center_m'],collision=False,sets=sets,role='wet_fringe',minimum_spacing_m=.8,ownership='Authored local wet-bank grass/fern; no existing vegetation or PCG regeneration')
geometry_file=SOURCE/'StarterWorld.H06.wetland.json';assert not geometry_file.exists();geometry_text=json.dumps(surface,indent=2);geometry_sha=hashlib.sha256(geometry_text.encode()).hexdigest()
waters=[dict(id=q['id'],geometry_source=geometry_file.name,geometry_id=q['id'],geometry_sha256=geometry_sha,center_m=[*q['seed_xy_m'],q['water_level_m']],mesh_asset=T.manifest['owned_content_root']+'/Geometry/SM_'+q['id'].replace('.','_'),material=T.manifest['owned_content_root']+'/Materials/M_StarterWaterReview',role='shallow_closed_retention_basin',collision_policy='QUERY_VISIBILITY_ONLY_PAWN_IGNORE',swimming_bound=False,water_level_m=q['water_level_m'],maximum_depth_m=q['maximum_depth_m'],area_m2=q['area_m2']) for q in surface['pools']]
branch=trails[1]['points_m'];j=safe.index(branch[0]);walk=safe[:j+1]+branch[1:]+list(reversed(branch[:-1]))+safe[j+1:]
p=dict(stable_id='H06',target_id='H06',region_id=plan['region_id'],display_name='Southern Retention Wetland',archetype='natural_shallow_pools_with_dry_benches_and_retreat_knoll',seed=106206,generation_version=1,layout_revision=1,stage='PLANNED',center_source='StarterWorld.plan.json#places/H06',boundary_source='StarterWorld.plan.json#places/H06/boundary_m',levels=place['levels'],risk_tier='MID_WETLAND',clusters=clusters,trails=trails,props=props,instance_groups=[group],water_surfaces=waters,entries=[dict(id='H06.Entry.South',position_m=[*south,T.height(*south)],role='south_dry_approach'),dict(id='H06.Exit.North',position_m=[*north,T.height(*north)],role='north_dry_retreat')],safe_buffer_m=22,spawn_exclusion=dict(settlement_radius_margin_m=25,road_margin_m=15,water_margin_m=25,structures='Native simple/complex capsule verification',terrain_max_slope=.17),server_authority=dict(policy='SERVER_ONLY',npc_contract='/Script/Project_JCharacter.Project_JNPCCharacter',status='NOT_BOUND',mass_spawner_used=False,reward_writer_added=False),quest_hooks=[dict(id='H06.WetlandTracks',status='AUTHORING_HOOK_ONLY',cluster_ids=[q['id'] for q in clusters])],reset_policy=dict(status='CANDIDATE_ONLY',leash_scope='30m dry clearing; safe passage outside40m',ownership='Existing server AI/gameplay integration pending'),surface_extent_m=400,functional_check_points_m=walk,functional_check_scope='Complete south-to-north dry land route and southern combat bench branch out/back; other branches and water traversal unverified',functional_check_timeout_s=max(300,math.ceil(sum(math.dist(a,b) for a,b in zip(walk,walk[1:]))/2+60)),terrain_policy='Preserve source heights; fill only connected natural basins to less than1m maximum depth, no terrain flattening',ground_query_mode='forest_floor_with_headroom',trail_headroom_check=True,art_complete=False,navigation_verified=False,gameplay_ready=False,hydrology_scope='Two shallow closed depressions. Flowing feeder channels/main-river connection, wet mud blend, final water shader and swimming are unfinished.')
backup=ROOT/'Saved/StarterWorld/BeforeWetlandHunt';backup.mkdir(parents=True,exist_ok=True);assert not (backup/f.name).exists();shutil.copy2(f,backup/f.name);geometry_file.write_bytes(geometry_text.encode());data['hunting_grounds'].append(p);assert data['hunting_grounds'][:-1]==before['hunting_grounds'];f.write_text(json.dumps(data,indent=2),encoding='utf-8');REPORT.write_text(json.dumps(dict(target_id='H06',clusters=clusters,entries=p['entries'],water_surfaces=waters,fringe_instances=len(fringe),existing_forest_modified=False,pcg_regenerated=False,walk_length_m=sum(math.dist(a,b) for a,b in zip(walk,walk[1:])),height_sha256=T.manifest['terrain_profile']['sha256'],height_modified=False),indent=2),encoding='utf-8');print('H06 planned;',len(fringe),'fringe instances;',len(walk),'waypoints;',round(sum(math.dist(a,b) for a,b in zip(walk,walk[1:]))),'m')
