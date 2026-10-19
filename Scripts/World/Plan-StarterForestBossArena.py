"""B02 forest arena: retained trees, rooted ruins, boss movement and public bypass."""
import hashlib,importlib.util,json,math,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/MainWorld/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';FILE=OUT/'B02-boss-layout.json';assert not FILE.exists()
def module(name,file):
    s=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'MainWorld.manifest.json');routes=module('routes','Starter-HuntingRoutes.py')
plan=json.loads((SOURCE/T.manifest['layout_source']).read_text());place=next(p for p in plan['places'] if p['id']=='B02');assert place['center_m']==[6472,5630,455.5]
pre=json.loads((OUT/'B02-southern-forest-preflight.json').read_text());assets={m['path']:m for m in json.loads((OUT/'B02-forest-asset-preflight.json').read_text())['meshes']};assets.update({m['path']:m for m in json.loads((OUT/'H09-ruins-asset-preflight.json').read_text())['meshes']})
base='/Game/Map_Resource/AncientTempleRuins/Environment/';props=[]
for name,mesh,xy,scale,yaw,role in [
    ('OldCanopyTree',base+'Foliage/Ceiba_Tree_01/SM_Ceiba_Tree_01_A',[6506,5660],1.1,35,'hero_tree_outside_open_combat_pockets'),
    ('NorthernRootCover',base+'Foliage/CeibaRoots_01/SM_Ceiba_Roots_01_B',[6492,5650],.25,190,'exposed_root_cover'),
    ('WesternRootCover',base+'Foliage/CeibaRoots_01/SM_Ceiba_Roots_05',[6448,5646],.28,70,'exposed_root_cover'),
    ('RuinEntrance',base+'Construction/Wall/SM_Wall_01_Open_192_256_01_A',[6470,5596],1,0,'ancient_open_arch'),
    ('BrokenWestWall',base+'Construction/Wall/SM_Wall_01_Linear_512_512_01_A',[6440,5620],.6,65,'ruined_wall_cover'),
    ('EasternPillar',base+'Construction/Pillar/SM_Pillar_01_64_64_256_D',[6490,5606],1,10,'broken_ruin_pillar'),
]:
    b=assets[mesh];r=math.hypot(b['extent_cm'][0],b['extent_cm'][1])*scale/100+.05
    props.append(dict(id='B02.'+name,mesh=mesh,position_xy_m=xy,yaw_degrees=yaw,scale=[scale]*3,collision=True,conservative_radius_m=r,surface_mask_radius_m=3 if name=='OldCanopyTree' else r,role=role))
center=place['center_m'];arena=dict(id='B02.Arena',center_m=center,radius_m=32,engagement_candidate_radius_m=48,reset_candidate_radius_m=48,safe_exclusion_radius_m=60,terrain_modified=False)
obstacles=[dict(o,radius_m=max(o['radius_m'],2.5) if 'BlackAlder' in o['mesh'] else o['radius_m']) for o in pre['walking_mesh_bounds']]
obstacles.extend(dict(center_m=[*p['position_xy_m'],T.height(*p['position_xy_m'])],radius_m=p['conservative_radius_m']) for p in props)
R=routes.HuntingRoutes(T,place['boundary_m'],obstacles,[arena],width_m=2.8,grade_limit=.22)
south=[6448,5550];north=[6448,5712];stage=[6556,5634,T.height(6556,5634)]
assert all(R.clear(*q) for q in [south,north,stage[:2],center[:2]])
trails=[]
for name,role,a,b,safe in [
    ('SouthApproach','southern_arena_approach',south,center[:2],False),
    ('NorthRetreat','northern_arena_approach_and_retreat',north,center[:2],False),
    ('StagingApproach','safe_staging_to_fight',stage[:2],center[:2],False),
    ('WesternBypass','public_bypass_outside_candidate_reset',south,north,True),
    ('InnerCombatTraverse','forest_boss_movement_candidate',center[:2],[6458,5630],False),
]:
    trails.append(dict(id='B02.'+name,role=role,width_m=2.8,points_m=R.solve(a,b,safe)))
orbit=[[6478,5630],[6474,5636],[6464,5636],[6454,5630],[6464,5626],[6474,5626],[6478,5630]]
orbit_path=[]
for a,b in zip(orbit,orbit[1:]):
    segment=R.solve(a,b);orbit_path.extend(segment if not orbit_path else segment[1:])
trails.append(dict(id='B02.CombatCircuit',role='player_and_boss_orbit_candidate',width_m=2.8,points_m=orbit_path))
pockets=[dict(id='B02.MainCombatGap',center_m=center,radius_m=10),dict(id='B02.WestCombatGap',center_m=[6458,5630,T.height(6458,5630)],radius_m=6)]
staging=dict(id='B02.SafeStaging',center_m=stage,radius_m=6,arena_edge_clearance_m=math.dist(center[:2],stage[:2])-32-6,reset_candidate_edge_clearance_m=math.dist(center[:2],stage[:2])-48-6,points_m=[[stage[0]+u,stage[1]+v,T.height(stage[0]+u,stage[1]+v)] for u,v in [(-2,-2),(2,-2),(-2,2),(2,2)]])
for pocket in pockets:
    assert all(math.dist(pocket['center_m'][:2],o['center_m'][:2])>pocket['radius_m']+o['radius_m']+.6 for o in obstacles),'Open fight pocket intersects root/prop allowance'
walk=trails[0]['points_m']+list(reversed(trails[2]['points_m'][:-1]))+trails[2]['points_m'][1:]+trails[4]['points_m'][1:]+list(reversed(trails[4]['points_m'][:-1]))
# Include the complete combat circuit before leaving by the north retreat.
join=R.solve(center[:2],orbit_path[0]);walk+=join[1:]+orbit_path[1:]+list(reversed(join[:-1]))+list(reversed(trails[1]['points_m'][:-1]))+list(reversed(trails[3]['points_m'][:-1]))
regional=next(r for r in plan['roads'] if r['id']=='Road_Forest_Plateau')
P=dict(stable_id='B02',target_id='B02',region_id=plan['region_id'],display_name='Keeper of the Ancient Forest',seed=201302,generation_version=1,stage='PLANNED',levels=[28,30],arena=arena,combat_pockets=pockets,safe_staging=staging,trails=trails,props=props,entries=[dict(id='B02.Entry.South',position_m=[*south,T.height(*south)]),dict(id='B02.Entry.North',position_m=[*north,T.height(*north)])],encounter=dict(id='WorldBoss.'+plan['region_id']+'.B02',owner_class='/Script/Project_J.Project_JWorldEncounter',npc_class=None,activation_radius_cm=150000,respawn_seconds=300,status='EXISTING_SERVER_OWNER_INTERFACE_ONLY; NPC_UNBOUND',automatic_rewards=False,reset_ai_status='CANDIDATE_ONLY_NOT_BOUND',near_and_ranged_space='64m forest footprint with retained-tree cover, 10m/6m open pockets and an inner movement circuit; actual boss/attack/camera tuning pending'),regional_route_connection=dict(road_id=regional['id'],status='PHASE2_FOREST_TRAIL_TO_REGIONAL_ROAD_JOIN_PENDING; old review road retained'),quest_hooks=[dict(id='B02.DefeatForestKeeper',owner_event='OnResolved',status='NOT_BOUND')],retained_vegetation=dict(preflight='B02-southern-forest-preflight.json',count=len(pre['forest']),policy='Retain existing tree/shrub transforms; route around real trunk collision; Visibility-complex canopy is separate cover data'),height_sha256=T.manifest['terrain_profile']['sha256'],terrain_policy='Original height samples and mountains preserved. Root meshes are freestanding scaled cover, not underground-to-canopy parts misplaced at full height.',fall_management=dict(status='LOCAL_TERRAIN_RIM_SURVEY_PENDING'),surface_extent_m=210,functional_check_points_m=walk,functional_check_timeout_s=600,art_complete=False,gameplay_ready=False,navigation_verified=False)
f=SOURCE/T.manifest['boss_profiles'];data=json.loads(f.read_text());assert not any(p['target_id']=='B02' for p in data['bosses']);backup=ROOT/'Saved/StarterWorld/BeforeForestBossArena';backup.mkdir(parents=True,exist_ok=False);shutil.copy2(f,backup/f.name);data['bosses'].append(P);f.write_text(json.dumps(data,indent=2),encoding='utf-8');FILE.write_text(json.dumps(P,indent=2),encoding='utf-8');print('B02 planned',len(walk),'walk nodes,',len(trails),'trails,',len(props),'props')
