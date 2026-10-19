"""Add supported hillside homes, mountain workyard and a guarded lookout."""
import hashlib,importlib.util,json,math,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/MainWorld/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';FILE=OUT/'mountain-village-build.json';assert not FILE.exists(),'Inspect native results before another authoring pass'
def module(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
C=module('corridor','Build-StarterCorridor.py');S=module('settlement','Starter-SettlementAuthoring.py');T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'MainWorld.manifest.json')
P=next(p for p in json.loads((SOURCE/'MainWorld.settlements.json').read_text(encoding='utf-8'))['settlements'] if p['target_id']=='S06');assert P['stable_id']=='S05';cx,cy=next(p['center_m'][:2] for p in C.PLAN['places'] if p['id']=='S05');TAG='ProjectJ.Starter.V4.Mountain.v1';A=S.SettlementAuthoring(TAG,'StarterRegion/Settlements/S05/MountainBlockout');assert not any(TAG in [str(t) for t in a.tags] for a in A.subsystem.get_all_level_actors())
CUBE=C.mesh('/Engine/BasicShapes/Cube');STONE=unreal.load_asset('/Game/Map_Resource/MainWorld/Materials/M_MainWorldBridgeStone');WOOD=unreal.load_asset('/Game/Map_Resource/MedCastle/Materials/MI_WoodenPlanks2');assert STONE and WOOD
preflight=json.loads((OUT/'mountain-village-preflight.json').read_text(encoding='utf-8'));assert not any('error' in p for p in preflight['plots'])
R=dict(phase='QUEUED',target_id='S06',stable_id='S05',completed=[],houses=[],access=[],entries=[],height_sha256=T.manifest['terrain_profile']['sha256'],profile_sha256=hashlib.sha256((SOURCE/'MainWorld.settlements.json').read_bytes()).hexdigest(),art_complete=False,navigation_verified=False,performance_measured=False)
def record(phase):R.update(phase=phase,actors=A.created);FILE.write_text(json.dumps(R,indent=2),encoding='utf-8')
def tag(a):a.set_editor_property('tags',[unreal.Name(TAG),unreal.Name('ProjectJ.Location.S05'),unreal.Name('ProjectJ.Target.S06')]);return a
def empty(label,x,y,z,yaw=0):
    a=tag(A.spawn('S05_Mountain_'+label,'/Engine/BasicShapes/Cube',x,y,z,yaw));a.static_mesh_component.set_static_mesh(None);a.set_actor_transform(unreal.Transform(location=A.point(x,y,z),rotation=unreal.Rotator(pitch=0,yaw=yaw,roll=0),scale=unreal.Vector(1,1,1)),False,False);A.created[-1].update(mesh=None,location_cm=list(a.get_actor_location().to_tuple()));return a
def hism(a,name,mesh,material=None,collision=True):
    c=C.component(a,unreal.HierarchicalInstancedStaticMeshComponent,name);c.set_static_mesh(mesh);c.set_collision_profile_name('BlockAll' if collision else 'NoCollision')
    if material:c.set_material(0,A.instanced_material(material))
    else:
        for i,s in enumerate(mesh.static_materials):
            if s.material_interface:c.set_material(i,A.instanced_material(s.material_interface))
    return c
def house(data):
    dx,dy=data['offset_m'];h=dict(data,x=cx+dx,y=cy+dy,yaw=data['yaw_degrees'],roof_style='intact_thatch_gable',roof_contact_profile=P['roof_contact_profile']);a,r=A.supported_house('S05_Mountain_'+data['id'],h,T.height,C,STONE);tag(a);R['houses'].append(r);R['access'].append(dict(label=a.get_actor_label(),**A.add_access_stairs(a,r,T.height,C,STONE)))
def lookout():
    p=P['lookout'];x,y=cx+p['offset_m'][0],cy+p['offset_m'][1];w,d=p['width_m'],p['depth_m'];angle=math.radians(p['yaw_degrees']);co,si=math.cos(angle),math.sin(angle)
    def ground(u,v):return T.height(x+co*u-si*v,y+si*u+co*v)
    floor=max(ground(u,v) for u in [-w/2,0,w/2] for v in [-d/2,0,d/2])+.06;a=empty('LookoutDeck',x,y,floor,p['yaw_degrees']);groups={name:hism(a,name,CUBE,WOOD) for name in ['Planks','Underframe','Railings']}
    def box(name,u,v,bottom,sw,sd,sh):groups[name].add_instance(C.centered_transform(CUBE,unreal.Vector(u*100,v*100,(bottom-floor)*100),0,(sw,sd,sh)),False)
    for i in range(12):box('Planks',0,-d/2+(i+.5)*.5,floor-.2,w,.49,.2)
    for u in [-2.5,0,2.5]:
        box('Underframe',u,0,floor-.42,.22,d,.22)
        for v in [-2.5,2.5]:
            bottom=ground(u,v)-.3;box('Underframe',u,v,bottom,.24,.24,floor-.2-bottom)
    for u in [-2.85,2.85]:box('Railings',u,0,floor+.9,.12,5.7,.12)
    box('Railings',0,2.85,floor+.9,5.7,.12,.12)
    for u in [-1.975,1.975]:box('Railings',u,-2.85,floor+.9,1.75,.12,.12)
    for u,v in [(-2.85,-2.85),(-2.85,0),(-2.85,2.85),(0,2.85),(2.85,2.85),(2.85,0),(2.85,-2.85),(-1.05,-2.85),(1.05,-2.85)]:box('Railings',u,v,floor,.14,.14,1.08)
    placement=dict(id='Lookout',label=a.get_actor_label(),center_m=[x,y,floor],yaw=p['yaw_degrees'],width=w,depth=d);access=A.add_access_stairs(a,placement,T.height,C,STONE);R['lookout']=dict(placement,components={n:c.get_instance_count() for n,c in groups.items()},access=access)
def workyard():
    p=P['work_yards'][0];x,y=cx+p['offset_m'][0],cy+p['offset_m'][1];z=T.height(x,y);a=empty('OreSortingCourt',x,y,z);counts={}
    for name,path,count,scale in [('Granite','/Game/Map_Resource/Megascans/3D_Assets/GraniteRockAssembly/SM_GraniteRockAssembly',8,.6),('SmallStone','/Game/Map_Resource/Megascans/3D_Assets/HovsBeachRock/SM_HovsBeachRock',8,.5)]:
        mesh=C.mesh(path);c=hism(a,name,mesh,collision=False)
        for i in range(count):
            u=(i%4-1.5)*2.2;v=(i//4-1)*2.2+(2 if name=='SmallStone' else -1);c.add_instance(C.centered_transform(mesh,unreal.Vector(u*100,v*100,(T.height(x+u,y+v)-z)*100),i*47,(scale,)*3),False)
        counts[name]=count
    # A pedestal puts the existing anvil at working height.
    ax,ay=x+4,y+4;base=T.height(ax,ay);stand=tag(A.spawn('S05_Mountain_AnvilStand','/Engine/BasicShapes/Cube',ax,ay,base,0,(.8,.8,.6),STONE));comp=C.component(stand,unreal.StaticMeshComponent,'Anvil');obj=C.mesh('/Game/Map_Resource/Megascans/3D_Assets/BlacksmithAnvilA/SM_BlacksmithAnvilA');comp.set_static_mesh(obj);comp.set_relative_transform(C.centered_transform(obj,unreal.Vector(0,0,30)),False,False);comp.set_collision_profile_name('BlockAll')
    tag(A.spawn('S05_Mountain_OreCart','/Game/Map_Resource/SkyTown/Meshes/SM_Cart01',x+4,y-4,T.height(x+4,y-4),10));R['workyard']=dict(label=a.get_actor_label(),components=counts,gameplay_mining=False)
def entries():
    for name,street in [('West','WestContour'),('North','NorthHairpins')]:
        q=next(r['points_relative_m'][0] for r in P['streets'] if r['id']==street);x,y=cx+q[0],cy+q[1];label='Entry_S05_'+name;assert label not in A.labels;a=A.subsystem.spawn_actor_from_class(unreal.TargetPoint,A.point(x,y,T.height(x,y)+.1));assert a
        a.set_actor_label(label);a.set_folder_path(A.folder);tag(a);a.tags=list(a.tags)+[unreal.Name('ProjectJ.Entry.S05.'+name)];a.set_editor_property('is_spatially_loaded',True);a.set_editor_property('is_editor_only_actor',False);assert not a.get_editor_property('replicates');R['entries'].append(dict(label=label,path=a.get_path_name(),location_cm=list(a.get_actor_location().to_tuple())))
tasks=[(h['id'],lambda h=h:house(h)) for h in P['houses']]+[('Lookout',lookout),('Workyard',workyard),('Entries',entries)];index=0;busy=False
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
