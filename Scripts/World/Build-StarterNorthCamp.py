"""Author a saved boundary garrison, keeping all source terrain unchanged."""
import hashlib,importlib.util,json,math,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';FILE=OUT/'north-camp-build.json';assert not FILE.exists()
def module(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
C=module('corridor','Build-StarterCorridor.py');S=module('settlement','Starter-SettlementAuthoring.py');T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'StarterRegion.manifest.json');assert json.loads((OUT/'north-camp-registration.json').read_text())['phase']=='COMPLETE'
P=next(p for p in json.loads((SOURCE/'StarterWorld.settlements.json').read_text())['settlements'] if p['target_id']=='S09');place=next(v for v in C.PLAN['places'] if v['id']=='S08');cx,cy=place['center_m'][:2];TAG='ProjectJ.Starter.V4.NorthCamp.v1';A=S.SettlementAuthoring(TAG,'StarterRegion/Settlements/S09/NorthCampBlockout');assert not any(TAG in [str(t) for t in a.tags] for a in A.subsystem.get_all_level_actors())
CUBE=C.mesh('/Engine/BasicShapes/Cube');STONE=unreal.load_asset('/Game/World/Starter/Materials/M_StarterBridgeStone');WOOD=unreal.load_asset('/Game/Map_Resource/MedCastle/Materials/MI_WoodenPlanks2');assert STONE and WOOD
R=dict(phase='QUEUED',target_id='S09',stable_id='S08',completed=[],houses=[],tents=[],towers=[],access=[],entries=[],height_sha256=T.manifest['terrain_profile']['sha256'],profile_sha256=hashlib.sha256((SOURCE/'StarterWorld.settlements.json').read_bytes()).hexdigest(),art_complete=False,navigation_verified=False,performance_measured=False)
def record(phase):R.update(phase=phase,actors=A.created);FILE.write_text(json.dumps(R,indent=2),encoding='utf-8')
def tag(a):a.tags=[unreal.Name(TAG),unreal.Name('ProjectJ.Location.S08'),unreal.Name('ProjectJ.Target.S09')];return a
def foundation(a,q):
    c=C.component(a,unreal.StaticMeshComponent,'Foundation');c.set_static_mesh(CUBE);c.set_material(0,STONE);c.set_collision_profile_name('BlockAll');depth=q['center_m'][2]-q['foundation_bottom_m'];c.set_relative_transform(C.centered_transform(CUBE,unreal.Vector(0,0,-depth*100),0,(q['width']+.24,q['depth']+.24,depth)),False,False)
def house(q):
    dx,dy=q['offset_m'];h=dict(q,x=cx+dx,y=cy+dy,yaw=q['yaw_degrees'],roof_style='intact_thatch_gable',roof_contact_profile=P['roof_contact_profile']);a,r=A.supported_house('S09_North_'+q['id'],h,T.height,C,STONE);tag(a);R['houses'].append(r);R['access'].append(dict(label=a.get_actor_label(),**A.add_access_stairs(a,r,T.height,C,STONE)))
def tent(q):
    x,y,z=q['center_m'];a=tag(A.spawn('S09_North_'+q['id'],'/Engine/BasicShapes/Cube',x,y,z,q['yaw_degrees']));a.static_mesh_component.set_static_mesh(None);a.set_actor_transform(unreal.Transform(location=A.point(x,y,z),rotation=unreal.Rotator(pitch=0,yaw=q['yaw_degrees'],roll=0),scale=unreal.Vector(1,1,1)),False,False);A.created[-1].update(mesh=None,location_cm=list(a.get_actor_location().to_tuple()));foundation(a,q)
    c=C.component(a,unreal.StaticMeshComponent,'Tent');m=C.mesh(q['mesh']);c.set_static_mesh(m);c.set_collision_profile_name('NoCollision');scale=q['mesh_scale'];c.set_relative_transform(C.centered_transform(m,unreal.Vector(),0,(scale,scale,scale)),False,False)
    placement=dict(id=q['id'],label=a.get_actor_label(),center_m=q['center_m'],yaw=q['yaw_degrees'],width=q['width'],depth=q['depth'],foundation_bottom_m=q['foundation_bottom_m'],mesh=q['mesh'],mesh_scale=scale);R['tents'].append(placement);R['access'].append(dict(label=a.get_actor_label(),**A.add_access_stairs(a,placement,T.height,C,STONE)))
def tower(q):
    x,y,z=q['center_m'];s=q['scale'];a=tag(A.spawn('S09_North_'+q['id'],'/Engine/BasicShapes/Cube',x,y,z));a.static_mesh_component.set_static_mesh(None);a.set_actor_location(A.point(x,y,z),False,False);A.created[-1].update(mesh=None,location_cm=list(a.get_actor_location().to_tuple()));foundation(a,q)
    c=C.component(a,unreal.StaticMeshComponent,'Tower');m=C.mesh('/Game/Map_Resource/MedCastle/Mesh/Houses_Towers/SM_MainTower2Doors');c.set_static_mesh(m);c.set_collision_profile_name('BlockAll');c.set_relative_transform(C.centered_transform(m,unreal.Vector(),0,(s,s,s)),False,False)
    R['towers'].append(dict(q,label=a.get_actor_label()))
def gate():
    x,y,z=P['gate']['center_m'];bottom=max(q['center_m'][2] for q in P['gate']['towers'])+5.9;a=tag(A.spawn('S09_North_GateLintel','/Engine/BasicShapes/Cube',x,y,bottom,scale=(23,1.2,.65),material=STONE));R['gate']=dict(label=a.get_actor_label(),center_m=[x,y,bottom],passage_center_m=[x,y,z],clear_width_m=P['gate']['clear_width_m'],lintel_bottom_m=bottom,connection_id='E_N_Road')
    # Cloth is visual only. No invisible collision wall across the road.
    for i,q in enumerate(P['gate']['towers']):
        c=C.component(a,unreal.StaticMeshComponent,'Banner'+str(i));m=C.mesh('/Game/Map_Resource/SkyTown/Meshes/SM_Banner01');c.set_static_mesh(m);c.set_collision_profile_name('NoCollision')
        # Root has non-unit scale; compensate to keep a 0.45-scale hanging banner.
        world=C.centered_transform(m,A.point(q['center_m'][0],y-4.1,q['center_m'][2]+2),0,(.45,.45,.45));c.set_world_transform(world,False,False)
def supplies():
    for name,path,x,y in [('SupplyCrate','/Game/Map_Resource/SkyTown/Meshes/SM_FruitCrate01',3412,8033),('WaterBarrel','/Game/ElderboomVillage/Meshes/SM_Wooden_Barrel_uc2ichvqx_lod3',3414,8033)]:tag(A.spawn('S09_North_'+name,path,x,y,T.height(x,y),collision=False))
def entries():
    for name,q in [('SouthApproach',P['walk_route_points_m'][0]),('NorthBoundary',[P['gate']['port_position_m'][0],8058])]:
        x,y=q;label='Entry_S09_'+name;assert label not in A.labels;a=A.subsystem.spawn_actor_from_class(unreal.TargetPoint,A.point(x,y,T.height(x,y)+.1));assert a;a.set_actor_label(label);a.set_folder_path(A.folder);tag(a);a.set_editor_property('is_spatially_loaded',True);a.set_editor_property('is_editor_only_actor',False);R['entries'].append(dict(label=label,path=a.get_path_name(),location_cm=list(a.get_actor_location().to_tuple())))
    x,y,z=P['gate']['port_position_m'];a=A.subsystem.spawn_actor_from_class(unreal.TargetPoint,A.point(x,y,z));assert a;a.set_actor_label('Expansion_E_N_Road');a.set_folder_path(A.folder);tag(a);a.tags=list(a.tags)+[unreal.Name('ProjectJ.Connection.E_N_Road'),unreal.Name('Expansion.NeighborUnverified')];a.set_editor_property('is_spatially_loaded',True);a.set_editor_property('is_editor_only_actor',False);R['port_marker']=dict(label=a.get_actor_label(),path=a.get_path_name(),location_cm=list(a.get_actor_location().to_tuple()))
tasks=[(q['id'],lambda q=q:house(q)) for q in P['houses']]+[(q['id'],lambda q=q:tent(q)) for q in P['tents']]+[(q['id'],lambda q=q:tower(q)) for q in P['gate']['towers']]+[('Gate',gate),('Supplies',supplies),('Entries',entries)];index=0;busy=False
def tick(dt):
    global index,busy
    if busy:return
    busy=True
    try:
        if index==len(tasks):A.save();record('COMPLETE');unreal.unregister_slate_post_tick_callback(handle);return
        key,action=tasks[index];record('APPLYING_'+key);action();R['completed'].append(key);index+=1
        if index%2==0 or index==len(tasks):A.save()
        record('READY')
    except Exception:R['error']=traceback.format_exc();record('ERROR');unreal.unregister_slate_post_tick_callback(handle);unreal.log_error(R['error'])
    finally:busy=False
record('QUEUED');handle=unreal.register_slate_post_tick_callback(tick)
